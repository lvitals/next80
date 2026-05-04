#!/bin/sh
# Test runner for lk80 (linker) — covers MS-REL binary and XL3/ASlink text formats
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LK80="$SCRIPT_DIR/../../lk80/lk80"
N80="$SCRIPT_DIR/../../n80/n80"
N80_TESTS="$SCRIPT_DIR/../n80"

if [ ! -f "$LK80" ]; then
    echo "lk80 not found at $LK80 — build it first (cd lk80 && make)"
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

hex_of() { od -A n -t x1 -v "$1" | tr -s ' \n' ' ' | sed 's/^ *//;s/ *$//'; }

run_test() {
    local name="$1"
    local expected="$2"
    shift 2
    local out="$TMP/$name.bin"
    printf "  %-40s" "$name"
    if "$LK80" "$@" -o "$out" > /dev/null 2>&1; then
        actual=$(hex_of "$out")
        if [ "${actual#"$expected"}" != "$actual" ] || [ "$actual" = "$expected" ]; then
            echo "PASS"
            PASS=$((PASS + 1))
        else
            echo "FAIL"
            echo "      expected: $expected"
            echo "      actual:   $actual"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "FAIL (lk80 error)"
        FAIL=$((FAIL + 1))
    fi
}

verify_bin() {
    local name="$1"
    local bin="$2"
    local expected="$3"
    printf "  %-40s" "$name"
    actual=$(hexdump -v -e '/1 "%02x"' "$bin")
    if [ "$expected" = "$actual" ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL"
        echo "    expected: $expected"
        echo "    actual:   $actual"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== lk80 linker tests ==="

echo ""
echo "--- XL3 (ASlink/SDCC) format tests ---"

run_test "xlf_simple"     "3e 2a c9" \
    "$SCRIPT_DIR/xlf_simple.rel"

run_test "xlf_area_reloc" "3e 00 21 00 01" \
    "$SCRIPT_DIR/xlf_area_reloc.rel"

run_test "xlf_ext_cross"  "c9 cd 00 01" \
    "$SCRIPT_DIR/xlf_ext_a.rel" "$SCRIPT_DIR/xlf_ext_b.rel"

run_test "xlf_abs_area"   "42" \
    "$SCRIPT_DIR/xlf_abs.rel"

run_test "xlf_byte_reloc" "3e 00 26 01" \
    "$SCRIPT_DIR/xlf_byte_reloc.rel"

echo ""
echo "--- MS-REL binary format tests ---"

run_test "msrel_existing" \
    "$(hex_of "$SCRIPT_DIR/test_link.bin.ref")" \
    "$SCRIPT_DIR/test_z80.rel"

echo ""
echo "--- Assembly + link tests (using n80) ---"

echo "Assembling test sources..."
"$N80" "$N80_TESTS/test_reloc.mac"           "$TMP/test_reloc.rel"           --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_reloc_2.mac"        "$TMP/test_reloc_2.rel"         --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_escape.mac"         "$TMP/test_escape.rel"          --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_z280_pending.mac"   "$TMP/test_z280_pending.rel"    --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_z280_cpw.mac"       "$TMP/test_z280_cpw.rel"        --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_z280_epuf.mac"      "$TMP/test_z280_epuf.rel"       --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_z280_epui.mac"      "$TMP/test_z280_epui.rel"       --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_z280_epum.mac"      "$TMP/test_z280_epum.rel"       --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_z280_mepu.mac"      "$TMP/test_z280_mepu.rel"       --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_z280_tsti.mac"      "$TMP/test_z280_tsti.rel"       --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_ext_chain_size_a.mac" "$TMP/test_ext_chain_size_a.rel" --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/test_ext_chain_size_b.mac" "$TMP/test_ext_chain_size_b.rel" --build-type rel --no-show-banner

"$LK80" "$TMP/test_reloc.rel" "$TMP/test_reloc_2.rel" -o "$TMP/linked.bin" --code 100h
"$LK80" "$TMP/test_escape.rel"       -o "$TMP/test_escape.bin"       --output-format bin
"$LK80" "$TMP/test_z280_pending.rel" -o "$TMP/test_z280_pending.bin" --output-format bin
"$LK80" "$TMP/test_z280_cpw.rel"     -o "$TMP/test_z280_cpw.bin"     --output-format bin
"$LK80" "$TMP/test_z280_epuf.rel"    -o "$TMP/test_z280_epuf.bin"    --output-format bin
"$LK80" "$TMP/test_z280_epui.rel"    -o "$TMP/test_z280_epui.bin"    --output-format bin
"$LK80" "$TMP/test_z280_epum.rel"    -o "$TMP/test_z280_epum.bin"    --output-format bin
"$LK80" "$TMP/test_z280_mepu.rel"    -o "$TMP/test_z280_mepu.bin"    --output-format bin
"$LK80" "$TMP/test_z280_tsti.rel"    -o "$TMP/test_z280_tsti.bin"    --output-format bin
"$LK80" "$TMP/test_ext_chain_size_a.rel" "$TMP/test_ext_chain_size_b.rel" -o "$TMP/test_ext_chain_size.bin" --code 100h

verify_bin "test_escape"       "$TMP/test_escape.bin"       "c2a9f09f9880410a0d"
verify_bin "test_ext_chain_size" "$TMP/test_ext_chain_size.bin" "cd040141c9"
verify_bin "test_z280_cpw"     "$TMP/test_z280_cpw.bin"     "edc7edd7ede7ddede7fdede7edf7ddedc7fdedc70100fdedd7feffddedf70300ddedd73412fdedf73412"
verify_bin "test_z280_epuf"    "$TMP/test_z280_epuf.bin"    "ed97"
verify_bin "test_z280_epui"    "$TMP/test_z280_epui.bin"    "ed9f"
verify_bin "test_z280_epum"    "$TMP/test_z280_epum.bin"    "eda6ed8ced94ed9cedbc0100edac0200edb4fdffeda40400ed840500eda73412"
verify_bin "test_z280_mepu"    "$TMP/test_z280_mepu.bin"    "edaeed8ded95ed9dedbd0100edad0200edb5fdffeda50400ed850500edaf3412"
verify_bin "test_z280_tsti"    "$TMP/test_z280_tsti.bin"    "ed70"
verify_bin "test_z280_pending" "$TMP/test_z280_pending.bin" \
    "ed0aed12ed1added0added12dded1afded0afded12fded1a213412ed3a0100ed2a0200ed32fdffed220400ed020500dd213412dded3a0100dded2a0200dded32fdffdded220400dded020500fd213412fded3a0100fded2a0200fded32fdfffded220400fded020500ed6edded6efded6eed66dded66fded66ed87dded87fded87ed8fdded8ffded8fddcb0736fdcbf836"

echo ""
echo "--- Alignment tests ---"

"$N80" "$SCRIPT_DIR/align1.mac" "$TMP/align1.rel" --build-type rel --no-show-banner
"$N80" "$SCRIPT_DIR/align2.mac" "$TMP/align2.rel" --build-type rel --no-show-banner

run_test "alignment_msrel" "c1" \
    "$TMP/align1.rel" "$TMP/align2.rel" --code 100h --align-code 100h --align-data 100h

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
