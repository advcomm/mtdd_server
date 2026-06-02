# mtdd_server

Shard-side gRPC server for [@advcomm/mtdd](https://github.com/advcomm/mtdd). Each instance runs on a database host behind nginx, accepts `Connect` / `QueryStream` / `Disconnect`, executes SQL on **local PostgreSQL** via libpq, and streams results as FlexBuffers control metadata plus raw libpq cell bytes (RPGB v1 batches).

The same binary can expose **`MtddNotify`**, a coordinator-style LISTEN/NOTIFY transport matching the client’s `grpc-notify-client.ts` (client commit [78961be](https://github.com/advcomm/mtdd/commit/78961bee2d157e251cbae5867cf070cda9364919)).

## Requirements

- C++17 compiler
- CMake 3.20+
- gRPC, Protobuf, libpq, FlatBuffers

On Ubuntu 24.04:

```bash
sudo apt-get install -y build-essential cmake pkg-config \
  protobuf-compiler protobuf-compiler-grpc libgrpc++-dev libprotobuf-dev \
  libpq-dev libflatbuffers-dev libgtest-dev
```

Or use [vcpkg](https://vcpkg.io) with the included `vcpkg.json`:

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build -j
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Binary: `build/mtdd_server`

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `MTDD_LISTEN` | `unix:/run/mtdd/grpc.sock` (Linux) | Unix socket path (`unix:/path`) or dev-only `host:port` |
| `MTDD_ENV` | _(unset)_ | Set to `production` to enforce host index and unix socket listen |
| `MTDD_HOST_INDEX` | _(unset)_ | Required when `MTDD_ENV=production`; must match client `Connect.host_index` |
| `MTDD_PG_HOST` | `127.0.0.1` | libpq host (local Postgres) |
| `MTDD_POOL_SIZE` | `8` | Pool size for non-session queries |
| `MTDD_PG_FETCH_ROWS` | `10000` | Rows per PostgreSQL `FETCH` from the cursor |
| `MTDD_PG_WIRE_BATCH_ROWS` | `1000` | Rows per raw PG binary batch on the wire (each gRPC `BATCH` chunk) |
| `MTDD_PG_CONNECT_TIMEOUT_SEC` | `5` | libpq connect timeout |
| `MTDD_MAX_SESSIONS` | `512` | Pinned `session_id` connections |
| `MTDD_GRPC_MAX_THREADS` | CPU count | gRPC sync server threads |
| `MTDD_NOTIFY_ENABLED` | `1` | Register `MtddNotify` (`0` on shard-only nodes) |
| `MTDD_GRPC_REFLECTION` | `0` in production | Proto reflection for debugging |
| `MTDD_STATEMENT_TIMEOUT_MS` | `0` | PostgreSQL `statement_timeout` (0 = disabled) |
| `MTDD_MAX_QUERY_TEXT_BYTES` | `1048576` | Max SQL text size |
| `MTDD_MAX_NOTIFY_PAYLOAD_BYTES` | `65535` | Max NOTIFY payload |
| `MTDD_MAX_NOTIFY_CHANNEL_BYTES` | `63` | Max channel name length |
| `MTDD_HEALTH_PROBE_INTERVAL_SEC` | `30` | Periodic PostgreSQL probe after first Connect |
| `MTDD_HEALTH_REQUIRE_PG` | `1` | Set `0` on notify-only coordinator nodes |
| `MTDD_UNIX_SOCKET_MODE` | `660` | Octal permissions on the unix socket after bind |
| `MTDD_UNIX_SOCKET_DIR_MODE` | `750` | Octal permissions when creating the socket directory |
| `MTDD_UNIX_SOCKET_CREATE_DIR` | `0` | Create the socket parent directory if missing |
| `MTDD_ALLOW_TCP_LISTEN` | `0` | Allow `host:port` listen in production (dev only) |

Database credentials are supplied by the client in `Connect` (from app `DB_NAME`, `DB_USER`, `DB_PASSWORD`, `DB_PORT`). See [Initial Connect RPC payload](docs/OPERATIONS.md#initial-connect-rpc-payload) for the exact request shape and server field mapping.

**TLS and compression** are handled by nginx in front of this process. `mtdd_server` uses plain gRPC over a unix domain socket only — do not set `MTDD_GRPC_TLS*`. Clients verify nginx with [@advcomm/mtdd TLS env vars](https://github.com/advcomm/mtdd/blob/main/docs/OPERATIONS.md#tls-client--nginx) (commit [78961be+](https://github.com/advcomm/mtdd/commit/78961bee2d157e251cbae5867cf070cda9364919)). See [docs/OPERATIONS.md](docs/OPERATIONS.md).

### Production example

```bash
export MTDD_ENV=production
export MTDD_LISTEN=unix:/run/mtdd/grpc.sock
export MTDD_HOST_INDEX=0
export MTDD_PG_HOST=127.0.0.1
export MTDD_NOTIFY_ENABLED=1
export MTDD_GRPC_REFLECTION=0
```

## Client setup

Production apps must send `QueryRequest.result_format = 1` (libpq binary) and decode RPGB v1 batches from `ResultChunk.payload`:

```bash
export MTDD_GRPC_PORT=50051
export DB_HOST='["10.0.1.10","10.0.1.11"]'
# Multi-shard: point all apps at one notify coordinator
export MTDD_NOTIFY_URL=10.0.0.100:50051
node --require @advcomm/mtdd/register app.js
```

## LISTEN / NOTIFY

`LISTEN`, `UNLISTEN`, and `NOTIFY` SQL is handled **client-side** — it never goes through `QueryStream`. The client uses **`MtddNotify`** gRPC when not mocking:

| RPC | Purpose |
|-----|---------|
| `Subscribe` | Register `client_id` on `channel` + `tid_scope` |
| `Unsubscribe` | Remove one channel subscription |
| `UnsubscribeAll` | Clear all subscriptions for `client_id` |
| `Publish` | Fan out `{ channel, payload, process_id }` to subscribers |
| `Watch` | Server stream of notifications for `client_id` |

### Coordinator deployment

Notify subscriptions are stored **in process memory**. All subscribed clients must use the **same coordinator endpoint**:

- **Single shard:** client defaults to first `DB_HOST` write IP + `MTDD_GRPC_PORT` (no `MTDD_NOTIFY_URL` needed).
- **Multi-shard:** set `MTDD_NOTIFY_URL` on every app to one host. Run notify on that host only, or set `MTDD_NOTIFY_ENABLED=0` on other shards.

See [deploy/nginx/mtdd-notify-coordinator.conf](deploy/nginx/mtdd-notify-coordinator.conf) and [deploy/systemd/mtdd-notify-coordinator.service](deploy/systemd/mtdd-notify-coordinator.service).

After a notify `Watch` stream drops, the client reconnects and re-issues `Subscribe` for known channels. Subscriptions persist on the server until `Unsubscribe` / `UnsubscribeAll`.

### Disconnect semantics

`Disconnect` acknowledges client channel teardown only. It does **not** drain the shared connection pool or pinned sessions used by other app instances on the same shard.

## Deployment

1. Run PostgreSQL on localhost on each shard VM.
2. Run `mtdd_server` on a unix domain socket (`MTDD_LISTEN=unix:/run/mtdd/grpc.sock`, `MTDD_ENV=production`).
3. Configure nginx HTTP/2 gRPC proxy on the host IP, proxying to the unix socket — see [deploy/nginx/mtdd-grpc.conf](deploy/nginx/mtdd-grpc.conf) or [deploy/nginx/mtdd-grpc-tls.conf](deploy/nginx/mtdd-grpc-tls.conf) for TLS.
4. Set `MTDD_HOST_INDEX` to the shard’s index in `DB_HOST` — see [deploy/systemd/mtdd-server.service](deploy/systemd/mtdd-server.service).

```mermaid
flowchart LR
  App[Node app + mtdd] -->|gRPC TLS :443| Nginx[nginx on shard IP]
  Nginx -->|unix socket| Server[mtdd_server plain gRPC]
  Server -->|libpq| PG[(PostgreSQL)]
  App -->|MTDD_NOTIFY_URL| NotifyCoord[notify coordinator]
```

### Health checks

gRPC health starts `NOT_SERVING` until the first successful `Connect` probes PostgreSQL. Use `grpc.health.v1.Health/Check` for load balancers and orchestrators. Notify-only coordinators with `MTDD_HEALTH_REQUIRE_PG=0` report `SERVING` at startup.

## Proto sync

[proto/mtdd.proto](proto/mtdd.proto) is the **source of truth**. The client copies from this repo via its `scripts/sync-proto.sh`. Verify alignment with the paired client release:

```bash
./scripts/sync-proto.sh
# MTDD_PROTO_REF=1233831e2596f6496a246e695198921920771728  (default)
```

Run manually from GitHub Actions → build → Run workflow (automatic CI on push/PR is disabled).

## Integration test (Docker)

```bash
docker compose up --build --abort-on-container-exit integration
```

Compose profiles:

```bash
# Notify-only coordinator (no Postgres health requirement)
docker compose --profile notify-coordinator up notify_coordinator

# Shard without MtddNotify
docker compose --profile shard-only up shard_only
```

## Wire format

`QueryStream` pipelines raw libpq cell batches over gRPC (RPGB v1 in `ResultChunk.payload`). Each batch holds up to `MTDD_PG_WIRE_BATCH_ROWS` rows in column-major order: per cell, `uint8 is_null`, then `uint32 len` + raw `PQgetvalue` bytes. RPGB framing uses **little-endian** `uint32`; PG cell bytes are opaque libpq payloads (integers/date/timestamp **big-endian** in PG binary; floats follow PostgreSQL host byte order — see [docs/OPERATIONS.md](docs/OPERATIONS.md)). PostgreSQL cursors fetch up to `MTDD_PG_FETCH_ROWS` rows per round trip. Chunks: `SCHEMA` (FlexBuffers `ResultSchema` + optional first batch), `BATCH` (further batches), `TRAILER`, or `ERROR`.

## License

MIT — see [LICENSE](LICENSE).
