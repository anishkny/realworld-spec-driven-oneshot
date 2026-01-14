#!/usr/bin/env bash
set -euxo pipefail

PORT=3000

# Ensure port 5432 is up (Postgres)
npx -y wait-port 5432 --timeout 1000 || {
  echo "Make sure Postgres is running on port 5432"
  exit 1
}

# Ensure cleanup happens even if script fails
trap 'cleanup' EXIT
cleanup() {
  npx -y kill-port ${PORT} > /dev/null 2>&1 || true
}
cleanup

# Start server and run tests
npx -y start-server-and-test './code/start.sh' 3000 'node --test --test-timeout 10000 ./api.test.mjs'
