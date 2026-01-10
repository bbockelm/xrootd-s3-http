#!/bin/sh

# OFS Redirect Integration Test Setup
# This script sets up MinIO and an XRootD server with the OFS Redirect plugin
# to test presigned URL redirection from XRootD to S3

TEST_NAME=$1

if [ -z "$BINARY_DIR" ]; then
  echo "\$BINARY_DIR environment variable is not set; cannot run test"
  exit 1
fi
if [ ! -d "$BINARY_DIR" ]; then
  echo "$BINARY_DIR is not a directory; cannot run test"
  exit 1
fi
if [ -z "$SOURCE_DIR" ]; then
  echo "\$SOURCE_DIR environment variable is not set; cannot run test"
  exit 1
fi
if [ ! -d "$SOURCE_DIR" ]; then
  echo "\$SOURCE_DIR environment variable is not set; cannot run test"
  exit 1
fi

echo "Setting up OFS Redirect test environment for $TEST_NAME"

if [ -z "$MINIO_BIN" ]; then
  echo "minio binary not found; cannot run unit test"
  exit 1
fi

if [ -z "$MC_BIN" ]; then
  echo "mc binary not found; cannot run unit test"
  exit 1
fi

XROOTD_BIN="$XROOTD_BINDIR/xrootd"
if [ -z "$XROOTD_BIN" ]; then
  echo "xrootd binary not found; cannot run unit test"
  exit 1
fi

# Verify the OFS Redirect plugin library exists
OFSREDIRECT_LIB=""
for pattern in "$BINARY_DIR/libXrdOfsRedirect"*.so "$BINARY_DIR/lib/libXrdOfsRedirect"*.so; do
  for lib in $pattern; do
    if [ -f "$lib" ]; then
      OFSREDIRECT_LIB="$lib"
      break 2
    fi
  done
done

if [ -z "$OFSREDIRECT_LIB" ]; then
  echo "OFS Redirect plugin library not found in $BINARY_DIR or $BINARY_DIR/lib; cannot run test"
  exit 1
fi
echo "Using OFS Redirect plugin: $OFSREDIRECT_LIB"

# Check for XrdClS3 plugin configuration from CMake
# XRDCLS3_PLUGIN_CONF points to the directory containing s3-plugin.conf
if [ -n "$XRDCLS3_PLUGIN_CONF" ] && [ -f "$XRDCLS3_PLUGIN_CONF/s3-plugin.conf" ]; then
  echo "Using XrdClS3 plugin config from CMake: $XRDCLS3_PLUGIN_CONF/s3-plugin.conf"
  USE_CMAKE_XRDCLS3=1
else
  # Fall back to searching for installed XrdClS3 library
  USE_CMAKE_XRDCLS3=0
  XRDCLS3_LIB=""
  for pattern in "$XROOTD_LIBDIR/libXrdClS3"*.so; do
    for lib in $pattern; do
      if [ -f "$lib" ]; then
        XRDCLS3_LIB="$lib"
        break 2
      fi
    done
  done

  if [ -z "$XRDCLS3_LIB" ]; then
    echo "WARNING: XrdClS3 plugin library not found; S3 existence check may not work"
  else
    echo "Using XrdClS3 plugin: $XRDCLS3_LIB"
  fi
fi

mkdir -p "$BINARY_DIR/tests/$TEST_NAME"
RUNDIR=$(mktemp -d -p "$BINARY_DIR/tests/$TEST_NAME" test_run.XXXXXXXX)

if [ ! -d "$RUNDIR" ]; then
  echo "Failed to create test run directory"
  exit 1
fi

echo "Using $RUNDIR as the test run's home directory."
cd "$RUNDIR"

MINIO_DATADIR="$RUNDIR/minio-data"
MINIO_CLIENTDIR="$RUNDIR/minio-client"
MINIO_CERTSDIR="$RUNDIR/minio-certs"
XROOTD_CONFIGDIR="$RUNDIR/xrootd-config"
XROOTD_DATADIR="$RUNDIR/xrootd-data"

mkdir -p "$MINIO_DATADIR"
mkdir -p "$MINIO_CERTSDIR/ca"
mkdir -p "$MINIO_CERTSDIR/CAs"
mkdir -p "$MINIO_CLIENTDIR"
mkdir -p "$XROOTD_CONFIGDIR"
mkdir -p "$XROOTD_DATADIR/localfiles"

echo > "$BINARY_DIR/tests/$TEST_NAME/server.log"

# Create the TLS credentials for the test
openssl genrsa -out "$MINIO_CERTSDIR/tlscakey.pem" 4096 >> "$BINARY_DIR/tests/$TEST_NAME/server.log" 2>&1
touch "$MINIO_CERTSDIR/ca/index.txt"
echo '01' > "$MINIO_CERTSDIR/ca/serial.txt"

cat > "$MINIO_CERTSDIR/tlsca.ini" <<EOF
[ ca ]
default_ca = CA_test

[ CA_test ]
default_days = 365
default_md = sha256
private_key = $MINIO_CERTSDIR/tlscakey.pem
certificate = $MINIO_CERTSDIR/CAs/tlsca.pem
new_certs_dir = $MINIO_CERTSDIR/ca
database = $MINIO_CERTSDIR/ca/index.txt
serial = $MINIO_CERTSDIR/ca/serial.txt

[ req ]
default_bits = 4096
distinguished_name = ca_test_dn
x509_extensions = ca_extensions
string_mask = utf8only

[ ca_test_dn ]
commonName_default = Test CA

[ ca_extensions ]
basicConstraints = critical,CA:true
keyUsage = keyCertSign,cRLSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid

[ signing_policy ]
countryName            = optional
stateOrProvinceName    = optional
localityName           = optional
organizationName       = optional
organizationalUnitName = optional
commonName             = supplied
emailAddress           = optional

[ cert_extensions ]
basicConstraints = critical,CA:false
keyUsage = digitalSignature
extendedKeyUsage = critical, serverAuth, clientAuth
EOF

# Create the CA certificate
openssl req -x509 -key "$MINIO_CERTSDIR/tlscakey.pem" -config "$MINIO_CERTSDIR/tlsca.ini" \
  -out "$MINIO_CERTSDIR/CAs/tlsca.pem" -outform PEM -subj "/CN=Test CA" 0<&- >> "$BINARY_DIR/tests/$TEST_NAME/server.log" 2>&1
if [ "$?" -ne 0 ]; then
  echo "Failed to generate CA certificate"
  exit 1
fi

# Create the host certificate (used by both MinIO and XRootD)
openssl genrsa -out "$MINIO_CERTSDIR/private.key" 4096 >> "$BINARY_DIR/tests/$TEST_NAME/server.log" 2>&1
openssl req -new -key "$MINIO_CERTSDIR/private.key" -config "$MINIO_CERTSDIR/tlsca.ini" \
  -out "$MINIO_CERTSDIR/public.csr" -outform PEM -subj "/CN=$(hostname)" 0<&- >> "$BINARY_DIR/tests/$TEST_NAME/server.log" 2>&1
if [ "$?" -ne 0 ]; then
  echo "Failed to generate host certificate request"
  exit 1
fi

openssl ca -config "$MINIO_CERTSDIR/tlsca.ini" -batch -policy signing_policy -extensions cert_extensions \
  -out "$MINIO_CERTSDIR/public.crt" -infiles "$MINIO_CERTSDIR/public.csr" 0<&- >> "$BINARY_DIR/tests/$TEST_NAME/server.log" 2>&1
if [ "$?" -ne 0 ]; then
  echo "Failed to sign host certificate"
  exit 1
fi

# Copy the certificate for XRootD to use
cp "$MINIO_CERTSDIR/public.crt" "$XROOTD_CONFIGDIR/hostcert.crt"
cp "$MINIO_CERTSDIR/private.key" "$XROOTD_CONFIGDIR/hostcert.key"

# Set the MinIO root credentials
export MINIO_ROOT_USER=minioadmin
export MINIO_ROOT_PASSWORD=QXDEiQxQw8qY
MINIO_USER=miniouser
MINIO_PASSWORD=2Z303QCzRI7s
printf "%s" "$MINIO_USER" > "$RUNDIR/access_key"
printf "%s" "$MINIO_PASSWORD" > "$RUNDIR/secret_key"

# Launch MinIO
"$MINIO_BIN" --certs-dir "$MINIO_CERTSDIR" server --address "$(hostname):0" "$MINIO_DATADIR" 0<&- >> "$BINARY_DIR/tests/$TEST_NAME/server.log" 2>&1 &
MINIO_PID=$!
echo "MinIO daemon PID: $MINIO_PID"

# Wait for MinIO to start
sleep 1
MINIO_PORT=$(grep "API: " "$BINARY_DIR/tests/$TEST_NAME/server.log" | tr ':' ' ' | awk '{print $NF}' | tail -n 1)
IDX=0
while [ -z "$MINIO_PORT" ]; do
  sleep 1
  # Check if MinIO process is still running
  if ! kill -0 "$MINIO_PID" 2>/dev/null; then
    echo "MinIO process exited unexpectedly"
    cat "$BINARY_DIR/tests/$TEST_NAME/server.log"
    exit 1
  fi
  MINIO_PORT=$(grep "API: " "$BINARY_DIR/tests/$TEST_NAME/server.log" | tr ':' ' ' | awk '{print $NF}' | tail -n 1)
  IDX=$(($IDX+1))
  if [ $IDX -gt 1 ]; then
    echo "Waiting for MinIO to start ($IDX seconds so far) ..."
  fi
  if [ $IDX -eq 60 ]; then
    echo "MinIO failed to start - failing"
    exit 1
  fi
done
MINIO_URL=https://$(hostname):$MINIO_PORT
echo "MinIO API server started on $MINIO_URL"

# Configure MinIO
"$MC_BIN" --insecure --config-dir "$MINIO_CLIENTDIR" alias set adminminio "$MINIO_URL" "$MINIO_ROOT_USER" "$MINIO_ROOT_PASSWORD"
"$MC_BIN" --insecure --config-dir "$MINIO_CLIENTDIR" admin user add adminminio "$MINIO_USER" "$MINIO_PASSWORD"
"$MC_BIN" --insecure --config-dir "$MINIO_CLIENTDIR" alias set userminio "$MINIO_URL" "$MINIO_USER" "$MINIO_PASSWORD"
"$MC_BIN" --insecure --config-dir "$MINIO_CLIENTDIR" admin policy attach adminminio readwrite --user "$MINIO_USER"
"$MC_BIN" --insecure --config-dir "$MINIO_CLIENTDIR" mb userminio/test-bucket
if [ $? -ne 0 ]; then
  echo "Failed to create test bucket in MinIO server"
  exit 1
fi

# Create test files
echo "S3 Object Content" > "$RUNDIR/s3_object.txt"
echo "Local File Content" > "$XROOTD_DATADIR/localfiles/local_file.txt"

# Create local placeholder file for the S3 object
# The OFS redirect plugin requires the file to exist locally (for authorization)
# but will redirect to S3 for the actual content
mkdir -p "$XROOTD_DATADIR/s3"
touch "$XROOTD_DATADIR/s3/s3_object.txt"

# Upload test file to S3
"$MC_BIN" --insecure --config-dir "$MINIO_CLIENTDIR" cp "$RUNDIR/s3_object.txt" userminio/test-bucket/s3_object.txt
if [ $? -ne 0 ]; then
  echo "Failed to upload test file to MinIO"
  exit 1
fi

# Create XRootD configuration with OFS Redirect plugin
cat > "$XROOTD_CONFIGDIR/xrootd.cfg" <<EOF
# XRootD configuration for OFS Redirect testing
xrd.port any
all.export /

# The OFS Redirect plugin wraps the default filesystem (++ means stacking)
xrootd.fslib ++ $OFSREDIRECT_LIB

# HTTP/TLS configuration
xrd.protocol XrdHttp:any $XROOTD_LIBDIR/libXrdHttp-5.so
xrd.tlsca certfile $MINIO_CERTSDIR/CAs/tlsca.pem
xrd.tls $XROOTD_CONFIGDIR/hostcert.crt $XROOTD_CONFIGDIR/hostcert.key

http.cafile $MINIO_CERTSDIR/CAs/tlsca.pem
http.cert $XROOTD_CONFIGDIR/hostcert.crt
http.key $XROOTD_CONFIGDIR/hostcert.key

# Local filesystem path (for passthrough files)
oss.localroot $XROOTD_DATADIR

# OFS Redirect configuration - redirect /s3/ prefix to MinIO
ofsredirect.trace debug
ofsredirect.path_name /s3
ofsredirect.bucket_name test-bucket
ofsredirect.service_url $MINIO_URL
ofsredirect.region us-east-1
ofsredirect.url_style path
ofsredirect.access_key_file $RUNDIR/access_key
ofsredirect.secret_key_file $RUNDIR/secret_key
ofsredirect.expiration_secs 3600

# Logging
all.sitename ofsredirect_test
EOF

echo "XRootD configuration written to $XROOTD_CONFIGDIR/xrootd.cfg"

# Configure XrdClS3 plugin for the XRootD client
# This is needed because XrdOfsRedirect uses XrdCl::FileSystem to check S3 object existence
if [ "$USE_CMAKE_XRDCLS3" = "1" ]; then
  # Use the CMake-generated plugin configuration
  XRD_PLUGINCONFDIR_VALUE="$XRDCLS3_PLUGIN_CONF"
  echo "Using CMake-generated XrdClS3 plugin config from $XRD_PLUGINCONFDIR_VALUE"
elif [ -n "$XRDCLS3_LIB" ]; then
  # Create plugin config from found library
  mkdir -p "$XROOTD_CONFIGDIR/client.plugins.d"
  # Note: URL pattern must include wildcard (s3://*) for the plugin to be matched
  cat > "$XROOTD_CONFIGDIR/client.plugins.d/s3-plugin.conf" <<EOF
url = s3://*
lib = $XRDCLS3_LIB
enable = true
EOF
  XRD_PLUGINCONFDIR_VALUE="$XROOTD_CONFIGDIR/client.plugins.d"
  echo "XrdClS3 plugin configuration written to $XRD_PLUGINCONFDIR_VALUE/s3-plugin.conf"
else
  XRD_PLUGINCONFDIR_VALUE=""
  echo "WARNING: No XrdClS3 plugin configuration available"
fi

# Start XRootD
export X509_CERT_DIR="$MINIO_CERTSDIR/CAs"
export X509_CERT_FILE="$MINIO_CERTSDIR/CAs/tlsca.pem"
if [ -n "$XRD_PLUGINCONFDIR_VALUE" ]; then
  export XRD_PLUGINCONFDIR="$XRD_PLUGINCONFDIR_VALUE"
fi
# XrdClS3 environment variables for S3 existence check
export XRDCLS3_URLSTYLE=path
export XRDCLS3_ACCESSKEYLOCATION="$RUNDIR/access_key"
export XRDCLS3_SECRETKEYLOCATION="$RUNDIR/secret_key"
XROOTD_RUNDIR=$(mktemp -d -p /tmp xrootd_test.XXXXXXXX)
XROOTD_LOGDIR="$BINARY_DIR/tests/$TEST_NAME"
# Note: Don't use -n to avoid creating a subdirectory for logs
"$XROOTD_BIN" -c "$XROOTD_CONFIGDIR/xrootd.cfg" -l "$XROOTD_LOGDIR/xrootd.log" -s "$XROOTD_RUNDIR/xrootd.pid" 0<&- >> "$XROOTD_LOGDIR/xrootd_startup.log" 2>&1 &
XROOTD_PID=$!
echo "XRootD daemon PID: $XROOTD_PID"
# Wait for XRootD to start
sleep 2
XROOTD_PORT=""
IDX=0
while [ -z "$XROOTD_PORT" ]; do
  sleep 1
  # Check if XRootD process is still running
  if ! kill -0 "$XROOTD_PID" 2>/dev/null; then
    echo "XRootD process exited unexpectedly"
    echo "Startup log:"
    cat "$XROOTD_LOGDIR/xrootd_startup.log"
    echo "XRootD log:"
    cat "$XROOTD_LOGDIR/xrootd.log" 2>/dev/null || echo "(no log file)"
    exit 1
  fi
  # Log format: "xrootd ofsredirect_test@hostname:port initialization completed"
  XROOTD_PORT=$(grep -oP "@[^:]+:\K[0-9]+" "$XROOTD_LOGDIR/xrootd.log" 2>/dev/null | head -n 1)
  IDX=$(($IDX+1))
  if [ $IDX -gt 1 ]; then
    echo "Waiting for XRootD to start ($IDX seconds so far) ..."
  fi
  if [ $IDX -eq 30 ]; then
    echo "XRootD failed to start - check logs"
    cat "$XROOTD_LOGDIR/xrootd.log"
    exit 1
  fi
done

XROOTD_URL="https://$(hostname):$XROOTD_PORT"
echo "XRootD server started on $XROOTD_URL"

# Save test environment
cat > "$BINARY_DIR/tests/$TEST_NAME/setup.sh" <<EOF
MINIO_URL=$MINIO_URL
MINIO_PID=$MINIO_PID
XROOTD_URL=$XROOTD_URL
XROOTD_PID=$XROOTD_PID
ACCESS_KEY_FILE=$RUNDIR/access_key
SECRET_KEY_FILE=$RUNDIR/secret_key
X509_CA_FILE=$MINIO_CERTSDIR/CAs/tlsca.pem
RUNDIR=$RUNDIR
EOF

echo "Test environment saved to $BINARY_DIR/tests/$TEST_NAME/setup.sh"
echo "OFS Redirect test setup complete"
