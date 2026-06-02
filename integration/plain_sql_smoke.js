'use strict'

const grpc = require('@grpc/grpc-js')
const protoLoader = require('@grpc/proto-loader')
const { assertProtoExists } = require('./lib/proto-path')

const SERVER = process.env.MTDD_SERVER_ADDR || '127.0.0.1:50051'

function loadClient() {
  const protoPath = assertProtoExists()
  const packageDefinition = protoLoader.loadSync(protoPath, {
    keepCase: true,
    longs: String,
    enums: String,
    defaults: true,
    oneofs: true,
  })
  const proto = grpc.loadPackageDefinition(packageDefinition).mtdd
  const client = new proto.MtddShard(SERVER, grpc.credentials.createInsecure())
  return { client }
}

function queryStream(client, request, deadlineMs = 30000) {
  return new Promise((resolve, reject) => {
    const chunks = []
    const deadline = Date.now() + deadlineMs
    const call = client.QueryStream(request, { deadline })
    call.on('data', (chunk) => chunks.push(chunk))
    call.on('error', reject)
    call.on('end', () => resolve(chunks))
  })
}

async function main() {
  const { client } = loadClient()

  // Production client strips name via buildQueryRequestPayload; send raw wire request
  // to verify the server still rejects non-empty QueryRequest.name.
  const request = {
    host_index: 0,
    text: 'SELECT 1',
    name: 'prepared_stmt',
    row_mode: '',
    session_id: '',
    result_format: 1,
    params: [],
  }

  try {
    await queryStream(client, request)
    throw new Error('expected QueryStream to fail for prepared statement name')
  } catch (err) {
    const message = err.details || err.message || String(err)
    if (!message.includes('prepared statements are not supported')) {
      throw new Error(`unexpected error: ${message}`)
    }
  }

  console.log(JSON.stringify({ ok: true, rejected: 'prepared statement name' }))
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
