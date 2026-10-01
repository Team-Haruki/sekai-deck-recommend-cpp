#!/usr/bin/env bash
# Instrumented native build, Python + binding tests, gcovr. Ported from sonar.yml.
# Needs coverage and gcovr on PATH. Writes <out>/python.xml and <out>/cpp.xml (SonarQube
# generic format; sonar.yml maps them to sonar.python.coverage.reportPaths / sonar.coverageReportPaths).
# Usage: scripts/ci/coverage.sh [out-dir]   (default coverage)
set -euo pipefail
cd "$(dirname "$0")/../.."
out="${1:-coverage}"; mkdir -p "$out"
cmake -S . -B build-sonar -DCMAKE_BUILD_TYPE=Debug -DSEKAI_ENABLE_COVERAGE=ON
cmake --build build-sonar --parallel 2
export SEKAI_BINDING_DIR="$PWD/build-sonar"
python -m coverage run --source=tools/bench -m unittest discover -s tests -p 'test_bench_tools.py'
python -m coverage xml -o "$out/python.xml"
python -m unittest discover -s tests -p 'test_binding*.py'
gcovr --root . --filter 'src/' --exclude 'src/sekai_deck_recommend_wasm.cpp' \
  --sonarqube-metric line --sonarqube "$out/cpp.xml" --print-summary build-sonar
