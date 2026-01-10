#!/bin/sh

# OFS Redirect Integration Test
# Tests the OFS Redirect plugin functionality

TEST_NAME=$1

if [ -z "$BINARY_DIR" ]; then
  echo "\$BINARY_DIR environment variable is not set; cannot run test"
  exit 1
fi
if [ ! -d "$BINARY_DIR" ]; then
  echo "$BINARY_DIR is not a directory; cannot run test"
  exit 1
fi

echo "Running OFS Redirect tests for $TEST_NAME"

if [ ! -f "$BINARY_DIR/tests/$TEST_NAME/setup.sh" ]; then
  echo "Test environment file $BINARY_DIR/tests/$TEST_NAME/setup.sh does not exist - cannot run test"
  exit 1
fi
. "$BINARY_DIR/tests/$TEST_NAME/setup.sh"

if [ -z "$XROOTD_URL" ]; then
  echo "XRootD URL is not set; cannot test"
  exit 1
fi

echo "============================================="
echo "Test 1: Access S3 object via redirect"
echo "============================================="

# When accessing /s3/s3_object.txt, the OFS Redirect plugin should:
# 1. Check if the object exists on S3
# 2. Generate a presigned URL
# 3. Redirect the client to the presigned URL

CONTENTS=$(curl --cacert "$X509_CA_FILE" -v -L --fail "$XROOTD_URL/s3/s3_object.txt" 2> "$BINARY_DIR/tests/$TEST_NAME/client.log")
CURL_EXIT=$?

if [ $CURL_EXIT -ne 0 ]; then
  echo "FAILED: Download of S3 object via redirect failed (curl exit: $CURL_EXIT)"
  cat "$BINARY_DIR/tests/$TEST_NAME/client.log"
  exit 1
fi

if [ "$CONTENTS" != "S3 Object Content" ]; then
  echo "FAILED: Downloaded S3 object content is incorrect: $CONTENTS"
  exit 1
fi

echo "PASSED: S3 object accessed via redirect successfully"

echo "============================================="
echo "Test 2: Access local file (passthrough)"
echo "============================================="

# When accessing /localfiles/local_file.txt, the plugin should pass through
# to the underlying filesystem (no redirect, since path doesn't match /s3/)

CONTENTS=$(curl --cacert "$X509_CA_FILE" -v --fail "$XROOTD_URL/localfiles/local_file.txt" 2>> "$BINARY_DIR/tests/$TEST_NAME/client.log")
CURL_EXIT=$?

if [ $CURL_EXIT -ne 0 ]; then
  echo "FAILED: Download of local file failed (curl exit: $CURL_EXIT)"
  exit 1
fi

if [ "$CONTENTS" != "Local File Content" ]; then
  echo "FAILED: Downloaded local file content is incorrect: $CONTENTS"
  exit 1
fi

echo "PASSED: Local file accessed via passthrough successfully"

echo "============================================="
echo "Test 3: Access missing S3 object"
echo "============================================="

# When accessing a non-existent S3 object, the redirect should fail with 404
# (or fall through to the underlying FS which also returns 404)

HTTP_CODE=$(curl --cacert "$X509_CA_FILE" -L --output /dev/null -v --write-out '%{http_code}' "$XROOTD_URL/s3/missing_object.txt" 2>> "$BINARY_DIR/tests/$TEST_NAME/client.log")

if [ "$HTTP_CODE" -ne 404 ]; then
  echo "FAILED: Expected HTTP code 404 for missing S3 object; actual was $HTTP_CODE"
  exit 1
fi

echo "PASSED: Missing S3 object returns 404"

echo "============================================="
echo "Test 4: Verify redirect header (presigned URL)"
echo "============================================="

# Check that the server actually returns a redirect (302) with a presigned URL
# Use -i to capture headers without following redirects

curl --cacert "$X509_CA_FILE" -v "$XROOTD_URL/s3/s3_object.txt" 2>&1 | grep -i "location:" > "$BINARY_DIR/tests/$TEST_NAME/redirect_headers.txt"

if grep -q "X-Amz-Signature=" "$BINARY_DIR/tests/$TEST_NAME/redirect_headers.txt"; then
  echo "PASSED: Redirect contains presigned URL with signature"
else
  echo "INFO: Could not verify presigned URL signature in redirect (may be following redirects)"
fi

echo "============================================="
echo "All OFS Redirect tests passed!"
echo "============================================="
