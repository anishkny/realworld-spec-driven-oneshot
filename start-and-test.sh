#!/usr/bin/env bash
set -euxo pipefail

PORT=3000
TIMEOUT=30000   # in milliseconds

# Install dependencies
npm install

# Ensure port is free
npx -y kill-port ${PORT} > /dev/null 2>&1 || true

# Start server in background
node src/index.js &

# Wait for server to be ready
npx -y wait-port http://localhost:${PORT} --output dots --timeout=${TIMEOUT}

# Run tests
npx -y mocha --bail --timeout 10000

# Kill the server process
npx -y kill-port ${PORT} > /dev/null 2>&1 || true
