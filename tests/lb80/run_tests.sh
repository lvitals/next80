#!/bin/sh
# Test runner for lb80 (library manager)
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LB80="$SCRIPT_DIR/../../lb80/lb80"
N80="$SCRIPT_DIR/../../n80/n80"
N80_TESTS="$SCRIPT_DIR/../n80"

if [ ! -f "$LB80" ]; then
    echo "lb80 not found at $LB80 — build it first (cd lb80 && make)"
    exit 1
fi
if [ ! -f "$N80" ]; then
    echo "n80 not found at $N80 — build it first (cd n80 && make)"
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

check_exit() {
    local name="$1"
    shift
    printf "  %-40s" "$name"
    if "$@" > /dev/null 2>&1; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (command failed)"
        FAIL=$((FAIL + 1))
    fi
}

check_file() {
    local name="$1"
    local path="$2"
    printf "  %-40s" "$name"
    if [ -f "$path" ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (file not found: $path)"
        FAIL=$((FAIL + 1))
    fi
}

check_output_contains() {
    local name="$1"
    local pattern="$2"
    shift 2
    printf "  %-40s" "$name"
    out=$("$@" 2>&1 || true)
    if echo "$out" | grep -q "$pattern"; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (pattern '$pattern' not found in output)"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== lb80 library manager tests ==="

"$N80" "$N80_TESTS/test_basic_z80.mac" "$TMP/mod1.rel" --build-type rel --no-show-banner 2>/dev/null
"$N80" "$N80_TESTS/test_macros.mac"    "$TMP/mod2.rel" --build-type rel --no-show-banner 2>/dev/null
"$N80" "$N80_TESTS/test_cond.mac"      "$TMP/mod3.rel" --build-type rel --no-show-banner 2>/dev/null

echo ""
echo "--- Create and list ---"
check_exit "create library (c)" \
    "$LB80" c "$TMP/test.lib" "$TMP/mod1.rel" "$TMP/mod2.rel"
ls -l "$TMP"
check_file "library file created" "$TMP/test.lib"
check_output_contains "list modules (l)" "Total:" \
    "$LB80" l "$TMP/test.lib"

echo ""
echo "--- Add module ---"
check_exit "add module (a)" \
    "$LB80" a "$TMP/test.lib" "$TMP/mod3.rel"
check_output_contains "list after add shows more modules" "Total:" \
    "$LB80" l "$TMP/test.lib"

echo ""
echo "--- Extract module ---"
check_exit "extract module (e)" \
    "$LB80" e "$TMP/test.lib" BASIC "$TMP/extracted.rel"
check_file "extracted file exists" "$TMP/extracted.rel"

echo ""
echo "--- Remove module ---"
check_exit "remove module (r)" \
    "$LB80" r "$TMP/test.lib" BASIC

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
