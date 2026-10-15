#!/usr/bin/env bash
# Create the self-signed root CA and the server certificate for the prod Battle.net service.
#
# Usage:  bash create-bnet-certs.sh [--out <dir>] [--force]
#
#   --out <dir>  where to write the files (default: $HOME/forever-certs, mode 0700)
#   --force      replace an existing CA. Every client that trusts the old root.crt
#                stops trusting the server, so only use this on purpose.
#
# Files written to <dir>:
#   root.crt       the CA certificate. Give this to the clients (Windows trust store).
#   root.key       the CA signing key. KEEP OFF THE SERVER and out of backups.
#   server.key     private key for trinity.actual.battle.net. Goes to the server.
#   fullchain.pem  server certificate followed by the CA. Goes to the server.
#
# The CA is limited by a name constraint to trinity.actual.battle.net, so it cannot be used
# to sign certificates for other names. Same settings as the Windows script it replaces.
#
# Not run as root. It needs no server access.
set -euo pipefail

PORTAL="trinity.actual.battle.net"
DAYS=3650
OUT="$HOME/forever-certs"
FORCE=0

while (( $# )); do
  case "$1" in
    --out)   OUT="${2:?--out needs a directory}"; shift ;;
    --force) FORCE=1 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

if [[ $EUID -eq 0 ]]; then
  echo "do not run as root: the CA key should stay with your own account" >&2
  exit 1
fi

command -v openssl >/dev/null || { echo "openssl not found" >&2; exit 1; }

if [[ -e "$OUT/root.key" && $FORCE -eq 0 ]]; then
  echo "$OUT/root.key already exists. Refusing to replace the CA (use --force to do it anyway)." >&2
  exit 1
fi

umask 077
mkdir -p "$OUT"
chmod 700 "$OUT"
cd "$OUT"

# Temporary config files, removed at the end.
cat > root.cnf <<EOF
[req]
distinguished_name = dn
prompt = no
x509_extensions = v3_ca

[dn]
O = Forever Repack
CN = Forever Repack Root CA $(hostname) ($PORTAL only)

[v3_ca]
basicConstraints = critical,CA:TRUE,pathlen:0
keyUsage = critical,keyCertSign,cRLSign
nameConstraints = critical,permitted;DNS:$PORTAL
subjectKeyIdentifier = hash
EOF

cat > server.ext <<EOF
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = DNS:$PORTAL
authorityKeyIdentifier = keyid
subjectKeyIdentifier = hash
EOF

openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days "$DAYS" \
  -keyout root.key -out root.crt -config root.cnf 2>/dev/null
openssl req -newkey rsa:2048 -nodes -sha256 \
  -keyout server.key -out server.csr -subj "/O=Forever Repack/CN=$PORTAL" 2>/dev/null
openssl x509 -req -in server.csr -CA root.crt -CAkey root.key -CAcreateserial \
  -out server.crt -days "$DAYS" -sha256 -extfile server.ext 2>/dev/null

echo "verify:"
openssl verify -CAfile root.crt server.crt
openssl x509 -in server.crt -noout -subject -ext subjectAltName

cat server.crt root.crt > fullchain.pem
chmod 600 root.key server.key
chmod 644 root.crt fullchain.pem
rm -f server.csr root.cnf server.ext root.srl server.crt
echo
echo "written to $OUT:"
ls -l "$OUT"
echo
echo "Next: copy server.key and fullchain.pem to the server (see README.md). root.key stays here."
