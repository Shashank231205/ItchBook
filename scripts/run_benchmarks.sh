#!/usr/bin/env bash
# Builds an optimised binary for this machine and runs the benchmark suite pinned to one core.
# For stable numbers: set the CPU governor to "performance" and disable turbo/SMT if possible.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPU="${CPU:-2}"

cmake --preset release -S "${ROOT}"
cmake --build --preset release

OUT="${ROOT}/build/release/benchmarks-$(date +%Y%m%d-%H%M%S).json"
taskset -c "${CPU}" "${ROOT}/build/release/benchmarks/itchbook_benchmarks" \
  --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true \
  --benchmark_out="${OUT}" \
  --benchmark_out_format=json \
  "$@"

echo "Results written to ${OUT}"
