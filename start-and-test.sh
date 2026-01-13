#!/usr/bin/env bash
# Script to start the server and run API tests
# Usage: ./start-and-test.sh

set -euo pipefail

# Configuration
PORT=3000
POSTGRES_PORT=5432
TEST_TIMEOUT=10000

# Ensure cleanup happens even if script fails
trap 'cleanup' EXIT

cleanup() {
  echo ""
  echo "==> Cleaning up..."
  npx -y kill-port ${PORT} > /dev/null 2>&1 || true
}

# ============================================================================
# Step 1: Check Prerequisites
# ============================================================================
echo "==> Checking prerequisites..."

if ! npx -y wait-port ${POSTGRES_PORT} --timeout 1000; then
  echo "ERROR: Postgres is not running on port ${POSTGRES_PORT}"
  echo "Please start Postgres before running this script."
  exit 1
fi

echo "✓ Postgres is running"

# ============================================================================
# Step 2: Prepare Environment
# ============================================================================
echo ""
echo "==> Preparing environment..."

# Ensure the server port is available
npx -y kill-port ${PORT} > /dev/null 2>&1 || true
echo "✓ Port ${PORT} is available"

# ============================================================================
# Step 3: Start Server
# ============================================================================
echo ""
echo "==> Starting server on port ${PORT}..."

./code/start.sh &

# Wait for server to be ready
if npx -y wait-port http://localhost:${PORT} --output dots; then
  echo "✓ Server is ready"
else
  echo "ERROR: Server failed to start"
  exit 1
fi

# ============================================================================
# Step 4: Run Tests
# ============================================================================
echo ""
echo "==> Running API tests..."

if node --test --test-timeout ${TEST_TIMEOUT} ./api.test.mjs; then
  echo ""
  echo "✓ All tests passed!"
  exit 0
else
  echo ""
  echo "✗ Tests failed"
  exit 1
fi
