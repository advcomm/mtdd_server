'use strict'

const fs = require('node:fs')
const path = require('node:path')

/** Repo root when running from integration/ (Docker: /work). */
function getRepoRoot() {
  return path.join(__dirname, '..', '..')
}

function getProtoPath() {
  return path.join(getRepoRoot(), 'proto', 'mtdd.proto')
}

function assertProtoExists() {
  const protoPath = getProtoPath()
  if (!fs.existsSync(protoPath)) {
    throw new Error(`proto not found at ${protoPath}`)
  }
  return protoPath
}

module.exports = {
  getRepoRoot,
  getProtoPath,
  assertProtoExists,
}
