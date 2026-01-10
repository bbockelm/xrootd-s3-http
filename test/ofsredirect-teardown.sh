#!/bin/sh

# OFS Redirect Integration Test Teardown
# Cleans up MinIO and XRootD processes

TEST_NAME=$1

if [ -z "$BINARY_DIR" ]; then
  echo "\$BINARY_DIR environment variable is not set; cannot run test"
  exit 1
fi
if [ ! -d "$BINARY_DIR" ]; then
  echo "$BINARY_DIR is not a directory; cannot run test"
  exit 1
fi

echo "Tearing down OFS Redirect test environment for $TEST_NAME"

if [ ! -f "$BINARY_DIR/tests/$TEST_NAME/setup.sh" ]; then
  echo "Test environment file $BINARY_DIR/tests/$TEST_NAME/setup.sh does not exist - cannot teardown"
  exit 1
fi
. "$BINARY_DIR/tests/$TEST_NAME/setup.sh"

if [ -n "$XROOTD_PID" ]; then
  echo "Stopping XRootD (PID: $XROOTD_PID)"
  kill "$XROOTD_PID" 2>/dev/null
fi

if [ -n "$MINIO_PID" ]; then
  echo "Stopping MinIO (PID: $MINIO_PID)"
  kill "$MINIO_PID" 2>/dev/null
fi

echo "OFS Redirect test teardown complete"
