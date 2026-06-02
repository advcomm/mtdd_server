# MTDD server operations

Companion to [@advcomm/mtdd docs/OPERATIONS.md](https://github.com/advcomm/mtdd/blob/main/docs/OPERATIONS.md).

## Plain SQL only

`mtdd_server` executes **plain SQL text** via libpq `PQexecParams`. Prepared statements (`QueryRequest.name` set) are rejected.

## Multi-shard LISTEN / NOTIFY

Subscriptions are stored in **one coordinator process**. On shard-only nodes:

```env
MTDD_NOTIFY_ENABLED=0
```

Run a dedicated coordinator (or one designated shard) with `MTDD_NOTIFY_ENABLED=1`. Client apps set:

```env
MTDD_NOTIFY_URL=10.0.0.100:50051
```

See [deploy/nginx/mtdd-notify-coordinator.conf](../deploy/nginx/mtdd-notify-coordinator.conf).

After a notify `Watch` stream drops, the client reconnects and re-issues `Subscribe` for known channels. The server keeps subscriptions across watch disconnects until `Unsubscribe` / `UnsubscribeAll`.

## Shutdown

Client `shutdownMtdd()` calls `Disconnect` per shard. Server `Disconnect` is **non-destructive** — it does not drain the shared connection pool or pinned sessions used by other app instances.

Server process shutdown: send `SIGTERM` / `SIGINT`; gRPC drains in-flight RPCs then exits.

## TLS

### nginx termination (trusted network default)

Keep `mtdd_server` on loopback with plain gRPC; terminate TLS at nginx — [deploy/nginx/mtdd-grpc.conf](../deploy/nginx/mtdd-grpc.conf).

### Native gRPC TLS (matches client `MTDD_GRPC_TLS_*`)

| Variable | Purpose |
|----------|---------|
| `MTDD_GRPC_TLS=1` | Enable TLS on the gRPC listener |
| `MTDD_GRPC_TLS_CERT_FILE` | Server certificate (PEM) |
| `MTDD_GRPC_TLS_KEY_FILE` | Server private key (PEM) |
| `MTDD_GRPC_TLS_CLIENT_CA_FILE` | Optional client CA for mTLS |

Client-side (same deployment):

| Variable | Purpose |
|----------|---------|
| `MTDD_GRPC_TLS=1` | Enable TLS for shard gRPC client |
| `MTDD_GRPC_TLS_CA_FILE` | CA bundle to verify server |
| `MTDD_GRPC_TLS_CERT_FILE` / `MTDD_GRPC_TLS_KEY_FILE` | Optional mTLS client cert |
| `MTDD_GRPC_TLS_SERVER_NAME` | SNI override |
| `MTDD_NOTIFY_TLS_*` | Notify coordinator TLS (falls back to `MTDD_GRPC_TLS_*`) |

Sample nginx TLS front-end: [deploy/nginx/mtdd-grpc-tls.conf](../deploy/nginx/mtdd-grpc-tls.conf).

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

## Integration tests

```bash
docker compose up --build --abort-on-container-exit integration
```
