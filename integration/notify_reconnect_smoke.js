'use strict'

const path = require('node:path')
const grpc = require('@grpc/grpc-js')
const protoLoader = require('@grpc/proto-loader')

const SERVER = process.env.MTDD_SERVER_ADDR || '127.0.0.1:50051'
const CLIENT_ID = `integration-reconnect-${Date.now()}`

function loadNotifyClient() {
  const protoPath = path.join(__dirname, '..', 'proto', 'mtdd.proto')
  const packageDefinition = protoLoader.loadSync(protoPath, {
    keepCase: true,
    longs: String,
    enums: String,
    defaults: true,
    oneofs: true,
  })
  const proto = grpc.loadPackageDefinition(packageDefinition).mtdd
  return new proto.MtddNotify(SERVER, grpc.credentials.createInsecure())
}

function promisifyUnary(client, method, request) {
  return new Promise((resolve, reject) => {
    client[method](request, (err, response) => {
      if (err) reject(err)
      else resolve(response)
    })
  })
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

async function main() {
  const client = loadNotifyClient()
  await sleep(2000)

  let watchCall = client.Watch({ client_id: CLIENT_ID })
  watchCall.on('error', () => {}) // ignore cancel on simulated disconnect

  const subscribe = await promisifyUnary(client, 'Subscribe', {
    client_id: CLIENT_ID,
    channel: 'reconnect_smoke',
    tid_scope: '__global__',
  })
  if (!subscribe.ok) {
    throw new Error(`Subscribe failed: ${subscribe.message}`)
  }

  await sleep(200)

  // Simulate watch drop (client reconnect path in grpc-notify-client.js)
  watchCall.cancel()
  await sleep(500)

  watchCall = client.Watch({ client_id: CLIENT_ID })
  watchCall.on('error', () => {})
  await sleep(200)

  // Re-subscribe after reconnect (matches client resubscribeAll)
  const resubscribe = await promisifyUnary(client, 'Subscribe', {
    client_id: CLIENT_ID,
    channel: 'reconnect_smoke',
    tid_scope: '__global__',
  })
  if (!resubscribe.ok) {
    throw new Error(`Re-subscribe failed: ${resubscribe.message}`)
  }

  const payload = `reconnect-${Date.now()}`
  const notificationPromise = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('timed out waiting for notification')), 5000)
    watchCall.on('data', (message) => {
      if (message.payload === payload) {
        clearTimeout(timer)
        resolve(message)
      }
    })
    watchCall.on('error', reject)
  })

  const publish = await promisifyUnary(client, 'Publish', {
    channel: 'reconnect_smoke',
    payload,
    tid_scope: '__global__',
  })
  if (!publish.ok || publish.delivered_count < 1) {
    throw new Error(`Publish failed or not delivered: ${JSON.stringify(publish)}`)
  }

  const notification = await notificationPromise
  watchCall.cancel()

  console.log(
    JSON.stringify({
      ok: true,
      channel: notification.channel,
      payload: notification.payload,
      deliveredCount: publish.delivered_count,
    }),
  )
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
