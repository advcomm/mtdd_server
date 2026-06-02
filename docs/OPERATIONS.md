# MTDD server operations

Companion to [@advcomm/mtdd docs/OPERATIONS.md](https://github.com/advcomm/mtdd/blob/main/docs/OPERATIONS.md) (client commit [1233831](https://github.com/advcomm/mtdd/commit/1233831e2596f6496a246e695198921920771728) — `grpc-query-codec`, RPGB + PG binary endian docs).

## Plain SQL only

`mtdd_server` executes **plain SQL text** via libpq `PQexecParams`. Prepared statements (`QueryRequest.name` set) are rejected with `prepared statements are not supported`.

## Initial Connect RPC payload

When `@advcomm/mtdd` preloads (`register` → `initGrpcHub`), it opens one gRPC channel per shard endpoint and immediately calls the unary **`Connect`** RPC. This is separate from the TCP/unix **transport** target (nginx or `MTDD_GRPC_UNIX_SOCKET`).

Flow:

1. `new MtddShard(address, tlsCreds)` — `address` is `DB_HOST` IP + `MTDD_GRPC_PORT`, or `MTDD_GRPC_UNIX_SOCKET` in local dev.
2. `Connect(ConnectRequest)` — first application RPC on that channel.
3. One `Connect` per write endpoint; optional `Connect` per read replica (read failures are logged, not fatal).
4. `Connect` uses `getGrpcConnectTimeoutMs()` deadline; retries apply to `QueryStream` only, not `Connect`.

### Client payload (production)

Built by `buildConnectRequest()` in client [`grpc-hub.ts`](https://github.com/advcomm/mtdd/blob/1233831/src/grpc-hub.ts). Credentials from `DB_NAME`, `DB_USER`, `DB_PASSWORD`, `DB_PORT` ([`grpc-credentials.ts`](https://github.com/advcomm/mtdd/blob/1233831/src/grpc-credentials.ts)):

```javascript
{
  host_index: hostIndex,              // 0-based index into validated DB_HOST array
  dbname: credentials.database,       // from DB_NAME
  database: credentials.database,     // duplicate alias (same value as dbname)
  user: credentials.user,             // from DB_USER
  password: credentials.password,     // from DB_PASSWORD
  port: credentials.port,             // from DB_PORT, default 5432
  host: roleHost,                     // write or read IP from DB_HOST entry
}
```

| Field | Source | Example |
|-------|--------|---------|
| `host_index` | Loop index over `DB_HOST` shards | `0`, `1`, … |
| `dbname` / `database` | `DB_NAME` | `"mydb"` |
| `user` | `DB_USER` | `"appuser"` |
| `password` | `DB_PASSWORD` | `"secret"` |
| `port` | `DB_PORT` (default 5432) | `5432` |
| `host` | Write or read IP from `DB_HOST` entry | `"10.0.0.5"` |

**Not sent** by the production client (proto3 defaults): `sslmode`, `connect_timeout`, `application_name`. gRPC TLS is configured via `MTDD_GRPC_TLS_*`, not `ConnectRequest.sslmode`.

### Server handling

| ConnectRequest field | Used by server? | Server source |
|---------------------|-----------------|---------------|
| `host_index` | Yes | Validated against `MTDD_HOST_INDEX` |
| `dbname` / `database` | Yes | Prefers `dbname`, falls back to `database` |
| `user`, `password`, `port` | Yes | Passed to libpq |
| `host` | **No** | Ignored; libpq host is `MTDD_PG_HOST` |
| `sslmode`, `connect_timeout`, `application_name` | **No** | Server uses `MTDD_PG_CONNECT_TIMEOUT_SEC`; hardcodes `application_name=mtdd_server` |

The client sends `host` (shard routing metadata) and Postgres `port`, but each `mtdd_server` instance always connects to **local** Postgres at `MTDD_PG_HOST`.

### Response

`ConnectResponse { ok, message }` — client rejects if `ok === false` or the RPC errors. On success the `MtddShard` stub stays open for `QueryStream` / `Disconnect`.

When `MTDD_GRPC_MOCK=1`, no real `Connect` RPC is sent.

## Query streaming

`QueryStream` uses PostgreSQL **cursors** (`DECLARE` / `FETCH FORWARD`) for `SELECT` / `WITH` / `TABLE` / `VALUES` queries. Tuple results are streamed as **raw libpq binary cells** (RPGB v1) in `ResultChunk.payload`; the client decodes PG OIDs locally. `QueryRequest.result_format` must be `1` (binary).

| Variable | Purpose |
|----------|---------|
| `MTDD_PG_FETCH_ROWS` | Rows per PostgreSQL `FETCH` (default `10000`) |
| `MTDD_PG_WIRE_BATCH_ROWS` | Rows per raw PG batch / gRPC chunk (default `1000`) |

Control chunks: `SCHEMA` (FlexBuffers `ResultSchema` + optional first batch), `BATCH` (RPGB v1 payload only), `TRAILER` (`ResultTrailer`), or `ERROR`.

Each `BATCH` payload is column-major: for each column and row, `uint8 is_null`, then `uint32 len` + raw `PQgetvalue` bytes (no server-side type conversion).

### Binary endianness

Two layers apply; the server only constructs the outer RPGB frame:

| Layer | Byte order | Handled by |
|-------|------------|------------|
| RPGB batch header and cell lengths | **Little-endian** `uint32` | `mtdd_server` (`raw_batch_encoder.cpp`) |
| PostgreSQL cell payloads inside each cell | **Per libpq / PG binary rules** | Client (`pg-binary-decode.ts`) |

Cell bytes are copied verbatim from libpq — the server never decodes or byte-swaps them.

PostgreSQL binary cells (`field.format = 1`), as decoded by the client:

| Types | Endianness |
|-------|------------|
| `int2`, `int4`, `int8`, `date`, `timestamp`, `timestamptz`, `numeric` | Big-endian |
| `float4`, `float8` | IEEE 754 in **PostgreSQL server native** byte order |
| `bool`, `bytea`, `uuid` | Opaque bytes (no multi-byte integer order) |
| Text (`format = 0`) | UTF-8 |

Production pairing assumes PostgreSQL runs on **little-endian** hosts (Linux x86_64 / aarch64), matching the client’s float decode path. Big-endian PostgreSQL would require client-side float endian detection (not implemented).

Command-only queries (`INSERT`, `UPDATE`, etc.) use a single execution and return `TRAILER` without row batches.

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

### Client local dev (direct unix socket)

When the app runs on the **same host** as a single-shard `mtdd_server` without nginx, the client can dial the socket directly:

| Variable | Purpose |
|----------|---------|
| `MTDD_GRPC_UNIX_SOCKET` | e.g. `/run/mtdd/grpc.sock` — plain gRPC to the server (matches `MTDD_LISTEN`) |

Do not combine with `MTDD_GRPC_TLS_*`. Multi-shard production must use nginx TCP per shard.

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

Default upstream ref: `1233831e2596f6496a246e695198921920771728` ([@advcomm/mtdd](https://github.com/advcomm/mtdd) recommended; minimum `@bced8d7` / `@07c20bc`).

Pair **@advcomm/mtdd@1233831** (or `@07c20bc`+) with **mtdd_server ≥ 765da45** (`≥ eac5748` recommended) for `QueryStream`.

## Integration tests

Docker Compose runs `mtdd_server` on a unix socket with an nginx sidecar:

```bash
docker compose up --build --abort-on-container-exit integration
```
