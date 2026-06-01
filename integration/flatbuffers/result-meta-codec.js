'use strict'

const flexbuffers = require('flatbuffers/js/flexbuffers')

function toFlexBytes(buffer) {
  const bytes = Buffer.isBuffer(buffer) ? buffer : Buffer.from(buffer)
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength)
}

function normalizeFieldInfo(field) {
  return {
    name: String(field.name ?? ''),
    table_oid: Number(field.table_oid ?? field.tableOid ?? 0) >>> 0,
    column_id: Number(field.column_id ?? field.columnId ?? 0) >>> 0,
    data_type_oid: Number(field.data_type_oid ?? field.dataTypeID ?? 0) >>> 0,
    format: Number(field.format ?? 0),
  }
}

function flexEncode(value) {
  const encoded = flexbuffers.encode(value)
  return Buffer.from(encoded.buffer, encoded.byteOffset, encoded.byteLength)
}

function decodeResultSchema(buffer) {
  const map = flexbuffers.toObject(toFlexBytes(buffer))
  const fields = Array.isArray(map.fields) ? map.fields.map(normalizeFieldInfo) : []
  return { command: map.command ?? 'SELECT', fields }
}

function decodeResultTrailer(buffer) {
  const map = flexbuffers.toObject(toFlexBytes(buffer))
  return {
    command_tag: map.command_tag ?? '',
    row_count: Number(map.row_count ?? 0),
    oid: Number(map.oid ?? 0) || null,
  }
}

function decodePgError(buffer) {
  const map = flexbuffers.toObject(toFlexBytes(buffer))
  return {
    sqlstate: map.sqlstate ?? '',
    severity: map.severity ?? 'ERROR',
    message: map.message ?? 'unknown error',
    detail: map.detail ?? '',
    position: map.position ?? '',
  }
}

module.exports = {
  decodeResultSchema,
  decodeResultTrailer,
  decodePgError,
}
