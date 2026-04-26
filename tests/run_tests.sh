#!/bin/sh
# Master test runner for next80 — runs all tests for n80, lk80, and lb80
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

TOTAL_PASS=0
TOTAL_FAIL=0
SUITES_FAILED=0

run_suite() {
    local name="$1"
    local script="$2"
    echo "========================================"
    echo "Running suite: $name"
    echo "========================================"
    if sh "$script"; then
        echo "Suite $name: OK"
    else
        echo "Suite $name: FAILED"
        SUITES_FAILED=$((SUITES_FAILED + 1))
    fi
    echo ""
}

run_suite "n80 (assembler)" "$SCRIPT_DIR/n80/run_tests.sh"
run_suite "lk80 (linker)"   "$SCRIPT_DIR/lk80/run_tests.sh"
run_suite "lb80 (librarian)" "$SCRIPT_DIR/lb80/run_tests.sh"

echo "========================================"
if [ "$SUITES_FAILED" -eq 0 ]; then
    echo "All test suites passed."
    exit 0
else
    echo "$SUITES_FAILED suite(s) had failures."
    exit 1
fi
