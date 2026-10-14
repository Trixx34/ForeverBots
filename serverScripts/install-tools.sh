#!/usr/bin/env bash
# Install build and runtime tools for the Forever server on Ubuntu 24.04 LTS.
# Run as root:  sudo bash install-tools.sh
# Idempotent: safe to run again.
#
# The package list is a starting point. The Phase 2 clean build is the test:
# any "Could not find" from CMake means a package is missing here.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "run as root" >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  ninja-build \
  git \
  ca-certificates \
  curl \
  pkg-config \
  python3 \
  python3-pip \
  libboost-all-dev \
  libssl-dev \
  libbz2-dev \
  zlib1g-dev \
  libreadline-dev \
  libncurses-dev \
  libmysqlclient-dev \
  libprotobuf-dev \
  protobuf-compiler \
  libzmq3-dev \
  logrotate \
  rsync \
  pigz \
  jq

# MySQL 8.4 server and client. The sim runs 8.4, so the server should too.
# Ubuntu's own mysql-server is 8.0. Install MySQL 8.4 from the MySQL APT repo
# (https://dev.mysql.com/downloads/repo/apt/) before running this line, or
# skip it and point the server at the NAS MySQL.
if ! command -v mysql >/dev/null 2>&1; then
  apt-get install -y mysql-client || true
  echo "NOTE: install the MySQL 8.4 server from the MySQL APT repo if this host runs its own DB."
fi

# Check the tools the build depends on.
for tool in cmake ninja g++ git python3 protoc; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "MISSING: $tool" >&2
    exit 1
  fi
done

echo "cmake:   $(cmake --version | head -1)"
echo "gcc:     $(g++ --version | head -1)"
echo "boost:   $(grep -m1 BOOST_LIB_VERSION /usr/include/boost/version.hpp | cut -d'"' -f2)"
echo "protoc:  $(protoc --version)"
echo "python3: $(python3 --version)"
echo "tools installed."
