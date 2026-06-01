'use strict'

const path = require('node:path')
const grpc = require('@grpc/grpc-js')
const protoLoader = require('@grpc/proto-loader')
const {
  decodeArrowStreamToPgResult,
  buildQueryRequestPayload,
} = require('./lib/grpc-arrow-client')

const SERVER = process.env.MTDD_SERVER_ADDR || '127.0.0.1:50051'
const PG = {
  host: process.env.PG_HOST || '127.0.0.1',
  port: Number(process.env.PG_PORT || 5432),
  user: process.env.PG_USER || 'mtdd',
  password: process.env.PG_PASSWORD || 'mtdd',
  database: process.env.PG_DB || 'mtdd_test',
}

function loadClient() {
  const protoPath = path.join(__dirname, '..', 'proto', 'mtdd.proto')
  const packageDefinition = protoLoader.loadSync(protoPath, {
    keepCase: true,
    longs: String,
    enums: String,
    defaults: true,
    oneofs: true,
  })
  const proto = grpc.loadPackageDefinition(packageDefinition).mtdd
  const client = new proto.MtddShard(SERVER, grpc.credentials.createInsecure())
  return { client, grpc }
}

function promisifyUnary(client, method, request) {
  return new Promise((resolve, reject) => {
    client[method](request, (err, response) => {
      if (err) reject(err)
      else resolve(response)
    })
  })
}

function queryStream(client, request, deadlineMs = 30000) {
  return new Promise((resolve, reject) => {
    const chunks = []
    const deadline = Date.now() + deadlineMs
    const call = client.QueryStream(request, { deadline })
    call.on('data', (chunk) => chunks.push(chunk))
    call.on('error', reject)
    call.on('end', () => {
      try {
        resolve(decodeArrowStreamToPgResult(chunks))
      } catch (err) {
        reject(err)
      }
    })
  })
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

async function main() {
  process.env.MTDD_GRPC_RESULT_FORMAT = 'arrow'
  const { client } = loadClient()

  await sleep(2000)

  const connectResponse = await promisifyUnary(client, 'Connect', {
    host_index: 0,
    dbname: PG.database,
    user: PG.user,
    password: PG.password,
    port: PG.port,
    host: '127.0.0.1',
  })

  if (!connectResponse.ok) {
    throw new Error(`Connect failed: ${connectResponse.message}`)
  }

  const createResult = await queryStream(
    client,
    buildQueryRequestPayload(0, {
      text: 'CREATE TABLE IF NOT EXISTS mtdd_smoke (id serial primary key, name text)',
      values: [],
    }),
  )
  if (createResult.command !== 'CREATE') {
    throw new Error(`unexpected command for CREATE: ${createResult.command}`)
  }

  await queryStream(
    client,
    buildQueryRequestPayload(0, {
      text: 'INSERT INTO mtdd_smoke (name) VALUES ($1), ($2)',
      values: ['alpha', 'beta'],
      types: [25, 25],
    }),
  )

  const selectResult = await queryStream(
    client,
    buildQueryRequestPayload(0, {
      text: 'SELECT id, name FROM mtdd_smoke ORDER BY id',
      values: [],
    }),
  )

  if (selectResult.rowCount < 2) {
    throw new Error(`expected at least 2 rows, got ${selectResult.rowCount}`)
  }
  if (!selectResult.fields.some((f) => f.name === 'name')) {
    throw new Error('missing name column in fields')
  }

  const sessionId = `integration-${Date.now()}`
  await queryStream(
    client,
    buildQueryRequestPayload(
      0,
      { text: 'BEGIN', values: [] },
      sessionId,
    ),
  )
  await queryStream(
    client,
    buildQueryRequestPayload(
      0,
      {
        text: 'INSERT INTO mtdd_smoke (name) VALUES ($1)',
        values: ['pinned'],
        types: [25],
      },
      sessionId,
    ),
  )
  await queryStream(
    client,
    buildQueryRequestPayload(
      0,
      { text: 'COMMIT', values: [] },
      sessionId,
    ),
  )

  const disconnect = await promisifyUnary(client, 'Disconnect', { host_index: 0 })
  if (!disconnect.ok) {
    throw new Error('Disconnect failed')
  }

  console.log(
    JSON.stringify({
      ok: true,
      selectRows: selectResult.rowCount,
      sampleName: selectResult.rows[0]?.name,
    }),
  )
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
