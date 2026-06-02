'use strict'

const grpc = require('@grpc/grpc-js')
const protoLoader = require('@grpc/proto-loader')
const { assertProtoExists } = require('./lib/proto-path')

const SERVER = process.env.MTDD_SERVER_ADDR || '127.0.0.1:50051'
const CLIENT_ID = `integration-notify-${Date.now()}`

function loadNotifyClient() {
  const protoPath = assertProtoExists()
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

function waitForNotification(watchCall, expectedPayload, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      reject(new Error(`timed out waiting for notification payload=${expectedPayload}`))
    }, timeoutMs)

    watchCall.on('data', (message) => {
      if (message.payload === expectedPayload) {
        clearTimeout(timer)
        resolve(message)
      }
    })
    watchCall.on('error', (err) => {
      clearTimeout(timer)
      reject(err)
    })
  })
}

async function main() {
  const client = loadNotifyClient()
  await sleep(2000)

  const watchCall = client.Watch({ client_id: CLIENT_ID })

  const subscribe = await promisifyUnary(client, 'Subscribe', {
    client_id: CLIENT_ID,
    channel: 'mtdd_smoke',
    tid_scope: '__global__',
  })
  if (!subscribe.ok) {
    throw new Error(`Subscribe failed: ${subscribe.message}`)
  }

  const payload = `notify-${Date.now()}`
  const notificationPromise = waitForNotification(watchCall, payload)

  await sleep(200)

  const publish = await promisifyUnary(client, 'Publish', {
    channel: 'mtdd_smoke',
    payload,
    tid_scope: '__global__',
  })
  if (!publish.ok) {
    throw new Error(`Publish failed: ${publish.message}`)
  }
  if (publish.delivered_count < 1) {
    throw new Error(`expected delivered_count >= 1, got ${publish.delivered_count}`)
  }

  const notification = await notificationPromise
  if (notification.channel !== 'mtdd_smoke') {
    throw new Error(`unexpected channel: ${notification.channel}`)
  }
  if (notification.process_id !== 0) {
    throw new Error(`expected synthetic process_id 0, got ${notification.process_id}`)
  }

  const unsubscribeAll = await promisifyUnary(client, 'UnsubscribeAll', {
    client_id: CLIENT_ID,
  })
  if (!unsubscribeAll.ok) {
    throw new Error(`UnsubscribeAll failed: ${unsubscribeAll.message}`)
  }

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
