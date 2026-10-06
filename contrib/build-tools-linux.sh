#!/usr/bin/env bash
# Builds ONLY the data-extraction tools for Linux (mapextractor, vmap4extractor,
# vmap4assembler, mmaps_generator). No server, no database needed.
#
# Intended to run inside a throwaway container so nothing is installed on the host, e.g. on the NAS:
#   docker run --rm -v /path/to/ForeverBots:/src -v /path/to/tools-out:/out ubuntu:24.04 \
#       bash /src/contrib/build-tools-linux.sh
# The binaries end up in /out (static-ish: they only need libstdc++/zlib/bzip2/boost shared libs
# that the container image provides; run them inside the same image, or see the note below).
#
# Note: the extractors read the CASC product `wow_classic_beta` from the WoW folder that holds
# `.build.info` and `_classic_beta_`, so run them with that folder as the working directory.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates pkg-config \
    libboost-system-dev libboost-filesystem-dev libboost-program-options-dev \
    libboost-thread-dev libboost-iostreams-dev libboost-regex-dev libboost-locale-dev \
    libboost-chrono-dev libboost-date-time-dev libboost-container-dev libboost-heap-dev \
    libboost-math-dev libboost-process-dev \
    libssl-dev libbz2-dev zlib1g-dev libreadline-dev libmysqlclient-dev

mkdir -p /tmp/build /out
cd /tmp/build
cmake /src -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DSERVERS=0 -DTOOLS=1 -DSCRIPTS=none -DWITH_WARNINGS=0 \
    -DCMAKE_INSTALL_PREFIX=/out
make -j"$(nproc)" install

ls -l /out /out/bin 2>/dev/null || true
echo "Done. Tools are in /out."
