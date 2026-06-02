'use strict'

const resultMeta = require('../flatbuffers/result-meta-codec')

const CHUNK_KIND_SCHEMA = 'CHUNK_KIND_SCHEMA'
const CHUNK_KIND_BATCH = 'CHUNK_KIND_BATCH'
const CHUNK_KIND_TRAILER = 'CHUNK_KIND_TRAILER'
const CHUNK_KIND_ERROR = 'CHUNK_KIND_ERROR'

function loadArrow() {
  return require('apache-arrow')
}

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
    name: req.name ?? '',
    row_mode: req.row_mode ?? '',
    session_id: sessionId ?? '',
    result_format: 0,
    params: buildLibpqQueryParams(req),
  }
}

function pgFieldsFromSchema(schema) {
  return (schema.fields ?? []).map((field) => ({
    name: field.name,
    dataTypeID: field.data_type_oid,
    tableID: field.table_oid,
    columnID: field.column_id,
    format: field.format,
  }))
}

function commandFromTag(commandTag, fallback) {
  if (!commandTag) return fallback ?? 'SELECT'
  const token = String(commandTag).trim().split(/\s+/)[0]
  return token ? token.toUpperCase() : fallback ?? 'SELECT'
}

function arrowValueToJs(value) {
  if (value === null || value === undefined) return null
  if (typeof value === 'bigint') return value.toString()
  if (value instanceof Date) return value
  if (typeof value === 'object' && value !== null && typeof value.toJSON === 'function') {
    return value.toJSON()
  }
  return value
}

function arrowTableToRows(table, fieldNames) {
  const names =
    fieldNames.length > 0 ? fieldNames : table.schema.fields.map((f) => f.name)
  const rows = []
  const rowCount = table.numRows ?? 0
  for (let rowIndex = 0; rowIndex < rowCount; rowIndex++) {
    const row = {}
    for (const name of names) {
      const column = table.getChild(name)
      row[name] = column ? arrowValueToJs(column.get(rowIndex)) : null
    }
    rows.push(row)
  }
  return rows
}

function decodeArrowStreamToPgResult(chunks) {
  let schema = null
  const ipcParts = []
  let trailer = null

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
      const ipc = chunk.arrow_ipc ?? chunk.arrowIpc
      if (ipc && ipc.length > 0) ipcParts.push(Buffer.from(ipc))
      continue
    }
    if (kind === CHUNK_KIND_BATCH || kind === 2) {
      const ipc = chunk.arrow_ipc ?? chunk.arrowIpc
      if (ipc && ipc.length > 0) ipcParts.push(Buffer.from(ipc))
      continue
    }
    if (kind === CHUNK_KIND_TRAILER || kind === 3) {
      const meta = chunk.flatbuffer_meta ?? chunk.flatbufferMeta
      if (meta && meta.length > 0) trailer = resultMeta.decodeResultTrailer(meta)
    }
  }

  const fieldNames = (schema?.fields ?? []).map((f) => f.name)
  const fields = pgFieldsFromSchema(schema ?? { fields: [] })
  let rows = []
  if (ipcParts.length > 0) {
    const arrow = loadArrow()
    const table = arrow.tableFromIPC(Buffer.concat(ipcParts))
    rows = arrowTableToRows(table, fieldNames)
  }

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
  decodeArrowStreamToPgResult,
}
