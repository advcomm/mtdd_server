'use strict'

const resultMeta = require('../flatbuffers/result-meta-codec')
const { decodeRawPgBatch } = require('./pg-binary-decode')

const CHUNK_KIND_SCHEMA = 'CHUNK_KIND_SCHEMA'
const CHUNK_KIND_BATCH = 'CHUNK_KIND_BATCH'
const CHUNK_KIND_TRAILER = 'CHUNK_KIND_TRAILER'
const CHUNK_KIND_ERROR = 'CHUNK_KIND_ERROR'

function encodeQueryParam(value, oid) {
  if (value === null || value === undefined) {
    return { oid: oid ?? 0, format: 0, value: Buffer.alloc(0) }
  }
  if (Buffer.isBuffer(value)) {
    return { oid: oid ?? 0, format: 1, value }
  }
  return { oid: oid ?? 0, format: 0, value: Buffer.from(String(value), 'utf8') }
}

function buildLibpqQueryParams(req) {
  const values = Array.isArray(req.values) ? req.values : []
  const types = Array.isArray(req.types) ? req.types : []
  return values.map((value, index) => encodeQueryParam(value, types[index]))
}

function buildQueryRequestPayload(hostIndex, req, sessionId) {
  return {
    host_index: hostIndex,
    text: req.text ?? '',
    name: '',
    row_mode: req.row_mode ?? '',
    session_id: sessionId ?? '',
    result_format: 1,
    params: buildLibpqQueryParams(req),
  }
}

function pgFieldsFromSchema(schema) {
  return (schema?.fields ?? []).map((field) => ({
    name: field.name,
    dataTypeID: field.data_type_oid,
    tableID: field.table_oid,
    columnID: field.column_id,
    format: field.format ?? 0,
  }))
}

function commandFromTag(commandTag, fallback) {
  if (!commandTag) return fallback ?? 'SELECT'
  const token = String(commandTag).trim().split(/\s+/)[0]
  return token ? token.toUpperCase() : fallback ?? 'SELECT'
}

function decodeQueryStreamToPgResult(chunks) {
  let schema = null
  let trailer = null
  const rows = []

  for (const chunk of chunks) {
    const kind = chunk.kind ?? chunk.Kind
    if (kind === CHUNK_KIND_ERROR || kind === 4) {
      const meta = chunk.flatbuffer_meta ?? chunk.flatbufferMeta
      if (meta && meta.length > 0) {
        const error = resultMeta.decodePgError(meta)
        throw new Error(error.message)
      }
      break
    }
    if (kind === CHUNK_KIND_SCHEMA || kind === 1) {
      const meta = chunk.flatbuffer_meta ?? chunk.flatbufferMeta
      if (meta && meta.length > 0) {
        schema = resultMeta.decodeResultSchema(meta)
      }
      const payload = chunk.payload ?? chunk.Payload
      if (schema && payload && payload.length > 0) {
        rows.push(...decodeRawPgBatch(Buffer.from(payload), schema.fields))
      }
      continue
    }
    if (kind === CHUNK_KIND_BATCH || kind === 2) {
      const payload = chunk.payload ?? chunk.Payload
      if (!schema) {
        throw new Error('BATCH chunk before SCHEMA')
      }
      if (payload && payload.length > 0) {
        rows.push(...decodeRawPgBatch(Buffer.from(payload), schema.fields))
      }
      continue
    }
    if (kind === CHUNK_KIND_TRAILER || kind === 3) {
      const meta = chunk.flatbuffer_meta ?? chunk.flatbufferMeta
      if (meta && meta.length > 0) {
        trailer = resultMeta.decodeResultTrailer(meta)
      }
    }
  }

  const fields = pgFieldsFromSchema(schema ?? { fields: [] })

  return {
    command: commandFromTag(trailer?.command_tag, schema?.command),
    rowCount: trailer?.row_count != null ? Number(trailer.row_count) : rows.length,
    oid: trailer?.oid ?? null,
    fields,
    rows,
  }
}

module.exports = {
  buildQueryRequestPayload,
  decodeQueryStreamToPgResult,
}
