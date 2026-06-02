'use strict'

const RAW_PG_BATCH_MAGIC = 0x42504752
const RAW_PG_BATCH_VERSION = 1

const OID_BOOL = 16
const OID_BYTEA = 17
const OID_INT8 = 20
const OID_INT2 = 21
const OID_INT4 = 23
const OID_DATE = 1082
const OID_TIMESTAMP = 1114
const OID_TIMESTAMPTZ = 1184
const OID_NUMERIC = 1700
const OID_FLOAT4 = 700
const OID_FLOAT8 = 701
const OID_UUID = 2950

const PG_DATE_TO_UNIX_EPOCH_DAYS = 10957
const PG_EPOCH_TO_UNIX_MICROS = 946684800000000n

function readU32(buffer, offset) {
  return buffer.readUInt32LE(offset)
}

function readI16BE(buffer, offset) {
  return buffer.readInt16BE(offset)
}

function readI32BE(buffer, offset) {
  return buffer.readInt32BE(offset)
}

function readI64BE(buffer, offset) {
  return buffer.readBigInt64BE(offset)
}

function readFloat4(buffer, offset) {
  return buffer.readFloatLE(offset)
}

function readFloat8(buffer, offset) {
  return buffer.readDoubleLE(offset)
}

function pgDateToJsDate(pgDays) {
  const unixDays = pgDays + PG_DATE_TO_UNIX_EPOCH_DAYS
  return new Date(unixDays * 86400000)
}

function pgTimestampToJsDate(pgMicros) {
  const unixMicros = pgMicros + PG_EPOCH_TO_UNIX_MICROS
  const ms = Number(unixMicros / 1000n) + Number(unixMicros % 1000n) / 1000
  return new Date(ms)
}

function decodeNumericBinary(buffer) {
  if (buffer.length < 8) {
    throw new Error('numeric binary too short')
  }
  const ndigits = readI16BE(buffer, 0)
  const weight = readI16BE(buffer, 2)
  const sign = readI16BE(buffer, 4)
  const dscale = readI16BE(buffer, 6)
  if (sign === 0xc000) {
    return null
  }

  let coeff = ''
  for (let i = 0; i < ndigits; i++) {
    const digit = readI16BE(buffer, 8 + i * 2)
    const exponent = weight - i
    if (exponent < 0) {
      continue
    }
    if (coeff === '') {
      coeff = String(digit)
      coeff += '0000'.repeat(exponent)
      continue
    }
    coeff += String(digit).padStart(4, '0')
    coeff += '0000'.repeat(Math.max(0, exponent - 1))
  }
  if (coeff === '') {
    coeff = '0'
  }
  if (dscale > 0) {
    while (coeff.length <= dscale) {
      coeff = '0' + coeff
    }
    coeff = coeff.slice(0, -dscale) + '.' + coeff.slice(-dscale)
  }
  if (sign === 0x4000 && coeff !== '0') {
    coeff = '-' + coeff
  }
  return coeff
}

function uuidBytesToString(bytes) {
  const hex = Buffer.from(bytes).toString('hex')
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`
}

function decodePgCell(field, bytes) {
  const oid = field.data_type_oid ?? field.dataTypeID ?? 0
  const format = field.format ?? 0

  if (bytes == null) {
    return null
  }

  if (format === 0) {
    const text = bytes.toString('utf8')
    if (oid === OID_BOOL) {
      return text === 't' || text === 'T' || text === '1'
    }
    if (oid === OID_INT2 || oid === OID_INT4 || oid === OID_INT8) {
      return text
    }
    if (oid === OID_FLOAT4 || oid === OID_FLOAT8 || oid === OID_NUMERIC) {
      return text
    }
    return text
  }

  switch (oid) {
    case OID_BOOL:
      return bytes.length > 0 && bytes[0] !== 0
    case OID_INT2:
      return readI16BE(bytes, 0)
    case OID_INT4:
      return readI32BE(bytes, 0)
    case OID_INT8:
      return readI64BE(bytes, 0).toString()
    case OID_FLOAT4:
      return readFloat4(bytes, 0)
    case OID_FLOAT8:
      return readFloat8(bytes, 0)
    case OID_BYTEA:
      return bytes
    case OID_DATE:
      return pgDateToJsDate(readI32BE(bytes, 0))
    case OID_TIMESTAMP:
    case OID_TIMESTAMPTZ:
      return pgTimestampToJsDate(readI64BE(bytes, 0))
    case OID_UUID:
      return uuidBytesToString(bytes)
    case OID_NUMERIC:
      return decodeNumericBinary(bytes)
    default:
      return bytes.toString('utf8')
  }
}

function decodeRawPgBatch(payload, fields) {
  if (!payload || payload.length === 0) {
    return []
  }

  const buffer = Buffer.isBuffer(payload) ? payload : Buffer.from(payload)
  if (buffer.length < 16) {
    throw new Error('raw PG batch too short')
  }

  const magic = readU32(buffer, 0)
  if (magic !== RAW_PG_BATCH_MAGIC) {
    throw new Error(`invalid raw PG batch magic: ${magic.toString(16)}`)
  }
  const version = readU32(buffer, 4)
  if (version !== RAW_PG_BATCH_VERSION) {
    throw new Error(`unsupported raw PG batch version: ${version}`)
  }

  const numRows = readU32(buffer, 8)
  const numCols = readU32(buffer, 12)
  if (numCols !== fields.length) {
    throw new Error(`column count mismatch: batch=${numCols} schema=${fields.length}`)
  }

  const rows = Array.from({ length: numRows }, () => ({}))
  let offset = 16

  for (let col = 0; col < numCols; col++) {
    const field = fields[col]
    const name = field.name
    for (let row = 0; row < numRows; row++) {
      if (offset >= buffer.length) {
        throw new Error('raw PG batch truncated')
      }
      const isNull = buffer[offset++] !== 0
      if (isNull) {
        rows[row][name] = null
        continue
      }
      if (offset + 4 > buffer.length) {
        throw new Error('raw PG batch truncated at length')
      }
      const valueLen = readU32(buffer, offset)
      offset += 4
      if (offset + valueLen > buffer.length) {
        throw new Error('raw PG batch truncated at value')
      }
      const valueBytes = buffer.subarray(offset, offset + valueLen)
      offset += valueLen
      rows[row][name] = decodePgCell(field, valueBytes)
    }
  }

  return rows
}

/** Match libpq binary column format for common OIDs (mock/tests). */
function mockWireFormat(dataTypeOid, pgFormat) {
  if (pgFormat != null && pgFormat !== 0) {
    return pgFormat
  }
  switch (dataTypeOid) {
    case OID_BOOL:
    case OID_INT2:
    case OID_INT4:
    case OID_INT8:
    case OID_FLOAT4:
    case OID_FLOAT8:
      return 1
    default:
      return 0
  }
}

module.exports = {
  RAW_PG_BATCH_MAGIC,
  RAW_PG_BATCH_VERSION,
  decodeRawPgBatch,
  decodePgCell,
  mockWireFormat,
}
