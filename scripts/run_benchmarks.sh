#!/usr/bin/env bash
set -euo pipefail

# LogForge Automated Benchmark Runner
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

CORPUS_SIZE_MIB=${1:-256}
FILES_COUNT=${2:-64}

echo "=========================================================="
echo "      LogForge Automated Build & Benchmark Suite          "
echo "=========================================================="
echo "Project Root: ${ROOT_DIR}"
echo "Corpus Size:  ${CORPUS_SIZE_MIB} MiB"
echo "File Count:   ${FILES_COUNT}"
echo ""

# 1. Configure and Build in Release mode
echo "[1/3] Building LogForge in Release mode (-O3)..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake -DCMAKE_BUILD_TYPE=Release "${ROOT_DIR}"
cmake --build . --config Release -- -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# 2. Run Unit Test Suite
echo ""
echo "[2/3] Executing Unit Test Suite..."
./indexer_tests

# 3. Execute Benchmarking Harness
echo ""
echo "[3/3] Running Systems Benchmark Harness..."
./indexer_bench "${CORPUS_SIZE_MIB}" "${FILES_COUNT}"

echo ""
echo "=========================================================="
echo "Benchmark run completed successfully."
echo "Tip: To test pure cold-cache I/O performance on Linux:"
echo "  sudo sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches'"
echo "  ./build/indexer_bench ${CORPUS_SIZE_MIB} ${FILES_COUNT}"
echo "=========================================================="
