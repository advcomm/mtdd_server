# MTDD server operations

Companion to [@advcomm/mtdd docs/OPERATIONS.md](https://github.com/advcomm/mtdd/blob/main/docs/OPERATIONS.md).

## Plain SQL only

`mtdd_server` executes **plain SQL text** via libpq `PQexecParams`. Prepared statements (`QueryRequest.name` set) are rejected with `prepared statements are not supported`.

## nginx + unix domain socket

`mtdd_server` does **not** terminate TLS or handle compression. It listens on a **unix domain socket** with plain gRPC only. nginx on the same host:

1. Accepts client traffic (HTTP/2, optional TLS on `:443`).
2. Terminates TLS and applies compression settings at the edge.
3. Proxies to `unix:/run/mtdd/grpc.sock` via `grpc_pass`.

Sample configs:

- Plain HTTP/2 front-end (lab): [deploy/nginx/mtdd-grpc.conf](../deploy/nginx/mtdd-grpc.conf)
- TLS front-end (production): [deploy/nginx/mtdd-grpc-tls.conf](../deploy/nginx/mtdd-grpc-tls.conf)

Systemd creates `/run/mtdd` via `RuntimeDirectory=mtdd`. Add the nginx user to the `mtdd` group so it can connect to the socket (`MTDD_UNIX_SOCKET_MODE=0660` by default).

| Variable | Purpose |
|----------|---------|
| `MTDD_LISTEN=unix:/run/mtdd/grpc.sock` | Unix socket path (required in production) |
| `MTDD_UNIX_SOCKET_MODE` | Octal mode applied after bind (default `660`) |
| `MTDD_UNIX_SOCKET_DIR_MODE` | Octal mode for auto-created directories (default `750`) |
| `MTDD_UNIX_SOCKET_CREATE_DIR` | Create parent directory if missing (default `0`; use `1` in containers without systemd) |

Startup checks for unix listen:

- Validates path length (107-byte Linux limit), rejects `..` and trailing `/`
- Ensures parent directory exists and is writable
- Detects live sockets (another running instance) vs stale files from crashes
- Verifies the socket node after bind and applies `MTDD_UNIX_SOCKET_MODE`
- Removes the socket file on graceful shutdown

Do **not** set `MTDD_GRPC_TLS*` on `mtdd_server` — startup rejects those variables.

### Client TLS (nginx CA)

Clients use the same TLS env vars as [@advcomm/mtdd](https://github.com/advcomm/mtdd) to verify **nginx**, not the backend:

| Variable | Purpose |
|----------|---------|
| `MTDD_GRPC_TLS=1` | Enable TLS for shard gRPC client |
| `MTDD_GRPC_TLS_CA_FILE` | CA bundle to verify nginx |
| `MTDD_GRPC_TLS_CERT_FILE` / `MTDD_GRPC_TLS_KEY_FILE` | Optional mTLS client cert |
| `MTDD_GRPC_TLS_SERVER_NAME` | SNI override |
| `MTDD_NOTIFY_TLS_*` | Notify coordinator TLS (falls back to `MTDD_GRPC_TLS_*`) |

## Multi-shard LISTEN / NOTIFY

Subscriptions are stored in **one coordinator process**. On shard-only nodes:

```env
MTDD_NOTIFY_ENABLED=0
```

Run a dedicated coordinator (or one designated shard) with `MTDD_NOTIFY_ENABLED=1`. Client apps set:

```env
MTDD_NOTIFY_URL=coordinator.example:50052
```

See [deploy/nginx/mtdd-notify-coordinator.conf](../deploy/nginx/mtdd-notify-coordinator.conf).

After a notify `Watch` stream drops, the client reconnects and re-issues `Subscribe` for known channels. The server keeps subscriptions across watch disconnects until `Unsubscribe` / `UnsubscribeAll`.

## Shutdown

Client `shutdownMtdd()` calls `Disconnect` per shard. Server `Disconnect` is **non-destructive** — it does not drain the shared connection pool or pinned sessions used by other app instances.

Server process shutdown: send `SIGTERM` / `SIGINT`; gRPC drains in-flight RPCs then exits.

## Health

gRPC health (`grpc.health.v1.Health/Check`) reports `NOT_SERVING` until the first successful shard `Connect` probes PostgreSQL. Notify-only coordinators may set `MTDD_HEALTH_REQUIRE_PG=0`.

## Proto sync

This repo is the **source of truth** for [proto/mtdd.proto](../proto/mtdd.proto). The client copies from here:

```bash
# On mtdd_server — verify match with client release
./scripts/sync-proto.sh

# On @advcomm/mtdd — pull from mtdd_server
MTDD_PROTO_REF=main ./scripts/sync-proto.sh
```

Default upstream ref: `9a9ae4f`.

## Integration tests

Docker Compose runs `mtdd_server` on a unix socket with an nginx sidecar:

```bash
docker compose up --build --abort-on-container-exit integration
```
