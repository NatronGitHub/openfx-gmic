#!/bin/bash
# Build a variant of the Linux CMake build (Ninja, the G'MIC sources from the workspace) in
# build-<name>, install it in install-<name>, and check its dependencies with check_linux_deps.sh
# (SHARED_DEPS passed through). The caller adds the variant's own checks and tests.
# Usage (from the top-level directory): linux_variant.sh <name> <CMake arguments>...
set -euo pipefail
name=${1:?usage: $0 <name> <CMake arguments>...}
shift
cmake -S . -B "build-$name" -G Ninja -DGMIC_DOWNLOAD_DIR="$PWD" "$@"
cmake --build "build-$name" -j4
cmake --install "build-$name" --prefix "install-$name"
.github/workflows/check_linux_deps.sh "install-$name/OFX/Plugins/GMIC.ofx.bundle"
