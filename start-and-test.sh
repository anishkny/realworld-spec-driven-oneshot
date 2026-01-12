#!/usr/bin/env bash
set -euxo pipefail

PORT=3000
TIMEOUT=30000   # in milliseconds

# Ensure port 5432 is up (Postgres)
npx -y wait-port 5432 --timeout 1000 || {
  echo "Make sure Postgres is running on port 5432"
  exit 1
}

# Ensure port is free
npx -y kill-port ${PORT} > /dev/null 2>&1 || true

# Start server in background
node src/index.js &

# Wait for server to be ready
npx -y wait-port http://localhost:${PORT} --output dots --timeout=${TIMEOUT}

# Run tests
node --test ./api.test.mjs

# Kill the server process
npx -y kill-port ${PORT} > /dev/null 2>&1 || true
