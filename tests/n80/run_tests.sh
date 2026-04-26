#!/bin/sh
# Test runner for n80 (assembler)
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
N80="$SCRIPT_DIR/../../n80/n80"
LK80="$SCRIPT_DIR/../../lk80/lk80"

if [ ! -f "$N80" ]; then
    echo "n80 not found at $N80 — build it first (cd n80 && make)"
    exit 1
fi
if [ ! -f "$LK80" ]; then
    echo "lk80 not found at $LK80 — build it first (cd lk80 && make)"
    exit 1
fi
if ! SDCC_VERSION=$(sdcc -v 2>/dev/null); then
    echo "sdcc not found — SDCC output tests will be skipped"
    SDCC_AVAILABLE=0
else
    SDCC_AVAILABLE=1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

[ "$SDCC_AVAILABLE" -eq 1 ] && echo "Using $(printf '%s\n' "$SDCC_VERSION" | head -n 1)"

run_rel_test() {
    local t="$1"
    printf "  %-45s" "$t"
    local out_rel="$TMP/${t%.mac}.rel"
    if "$N80" "$SCRIPT_DIR/$t" "$out_rel" --build-type rel --no-show-banner 2>/dev/null; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (assembly error)"
        FAIL=$((FAIL + 1))
    fi
}

run_bin_test() {
    local t="$1"
    local expected_hex="$2"
    printf "  %-45s" "$t"
    local out_rel="$TMP/${t%.mac}.rel"
    local out_bin="$TMP/${t%.mac}.bin"
    if ! "$N80" "$SCRIPT_DIR/$t" "$out_rel" --build-type rel --no-show-banner 2>/dev/null; then
        echo "FAIL (assembly error)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$LK80" "$out_rel" --output-file "$out_bin" 2>/dev/null; then
        echo "FAIL (link error)"
        FAIL=$((FAIL + 1))
        return
    fi
    actual_hex=$(od -v -A n -t x1 "$out_bin" | tr -d ' \n')
    if [ "$actual_hex" = "$expected_hex" ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (byte mismatch)"
        echo "    expected: $expected_hex"
        echo "    actual:   $actual_hex"
        FAIL=$((FAIL + 1))
    fi
}

run_sdcc_basic_test() {
    [ "$SDCC_AVAILABLE" -eq 0 ] && return
    local t="test_sdcc_output_basic.mac"
    printf "  %-45s" "$t (sdcc basic)"
    local out_rel="$TMP/${t%.mac}.rel"
    local out_bin="$TMP/${t%.mac}.bin"
    if ! "$N80" "$SCRIPT_DIR/$t" "$out_rel" --build-type sdcc --no-show-banner 2>/dev/null; then
        echo "FAIL (assembly error)"
        FAIL=$((FAIL + 1))
        return
    fi
    for pattern in \
        "^XL3$" \
        "^H 3 areas 2 global symbols$" \
        "^S \.__\.ABS\. Def000000$" \
        "^A _CODE size 3 flags 0 addr 0$" \
        "^S start Def000000$" \
        "^T 00 00 00 3E 42 C9$" \
        "^R 00 00 00 00$" \
        "^A _DATA size 2 flags 0 addr 0$" \
        "^T 00 00 00 11 22$" \
        "^A CUSTOM size 2 flags 4 addr 0$" \
        "^T 00 00 00 33 44$"
    do
        if ! grep -Eq "$pattern" "$out_rel"; then
            echo "FAIL (missing XL3 line: $pattern)"
            FAIL=$((FAIL + 1))
            return
        fi
    done
    if ! "$LK80" "$out_rel" --output-file "$out_bin" 2>/dev/null; then
        echo "FAIL (link error)"
        FAIL=$((FAIL + 1))
        return
    fi
    actual_hex=$(od -v -A n -t x1 "$out_bin" | tr -d ' \n')
    if [ "$actual_hex" = "3e42c911223344" ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (byte mismatch: $actual_hex)"
        FAIL=$((FAIL + 1))
    fi
}

run_sdcc_reloc_test() {
    [ "$SDCC_AVAILABLE" -eq 0 ] && return
    local t="test_sdcc_output_reloc.mac"
    printf "  %-45s" "$t (sdcc reloc)"
    local out_rel="$TMP/${t%.mac}.rel"
    local out_bin="$TMP/${t%.mac}.bin"
    if ! "$N80" "$SCRIPT_DIR/$t" "$out_rel" --build-type sdcc --no-show-banner 2>/dev/null; then
        echo "FAIL (assembly error)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! grep -Eq "^R 00 00 00 00 00 03 00 00$" "$out_rel"; then
        echo "FAIL (missing internal word relocation)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$LK80" "$out_rel" --output-file "$out_bin" 2>/dev/null; then
        echo "FAIL (link error)"
        FAIL=$((FAIL + 1))
        return
    fi
    actual_hex=$(od -v -A n -t x1 "$out_bin" | tr -d ' \n')
    if [ "$actual_hex" = "030100c9" ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (byte mismatch: $actual_hex)"
        FAIL=$((FAIL + 1))
    fi
}

run_sdcc_external_test() {
    [ "$SDCC_AVAILABLE" -eq 0 ] && return
    local a="test_sdcc_output_ext_a.mac"
    local b="test_sdcc_output_ext_b.mac"
    printf "  %-45s" "sdcc external relocation"
    local out_a="$TMP/${a%.mac}.rel"
    local out_b="$TMP/${b%.mac}.rel"
    local out_bin="$TMP/test_sdcc_output_ext.bin"
    if ! "$N80" "$SCRIPT_DIR/$a" "$out_a" --build-type sdcc --no-show-banner 2>/dev/null; then
        echo "FAIL ($a assembly error)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$N80" "$SCRIPT_DIR/$b" "$out_b" --build-type sdcc --no-show-banner 2>/dev/null; then
        echo "FAIL ($b assembly error)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! grep -Eq "^S _helper Def000000$" "$out_a" || ! grep -Eq "^S _helper Ref000000$" "$out_b"; then
        echo "FAIL (missing symbol definitions)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$LK80" "$out_a" "$out_b" --output-file "$out_bin" 2>/dev/null; then
        echo "FAIL (link error)"
        FAIL=$((FAIL + 1))
        return
    fi
    actual_hex=$(od -v -A n -t x1 "$out_bin" | tr -d ' \n')
    if [ "$actual_hex" = "c9cd0001c9" ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (byte mismatch: $actual_hex)"
        FAIL=$((FAIL + 1))
    fi
}

run_sdcc_byte_reloc_test() {
    [ "$SDCC_AVAILABLE" -eq 0 ] && return
    local t="test_sdcc_output_byte_reloc.mac"
    printf "  %-45s" "$t (sdcc byte reloc)"
    local out_rel="$TMP/${t%.mac}.rel"
    local out_bin="$TMP/${t%.mac}.bin"
    if ! "$N80" "$SCRIPT_DIR/$t" "$out_rel" --build-type sdcc --no-show-banner 2>/dev/null; then
        echo "FAIL (assembly error)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! grep -Eq "^R 00 00 00 00 09 03 00 00 89 06 00 00$" "$out_rel"; then
        echo "FAIL (missing LOW/HIGH relocations)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$LK80" "$out_rel" --output-file "$out_bin" 2>/dev/null; then
        echo "FAIL (link error)"
        FAIL=$((FAIL + 1))
        return
    fi
    actual_hex=$(od -v -A n -t x1 "$out_bin" | tr -d ' \n')
    if [ "$actual_hex" = "0201c9" ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (byte mismatch: $actual_hex)"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== n80 assembler tests ==="
echo ""
echo "--- REL format assembly ---"
for t in \
    test_basic_z80.mac \
    test_cpus.mac \
    test_macros.mac \
    test_cond.mac \
    test_reloc.mac \
    test_sdcc.mac \
    test_scope.mac \
    test_strings_phase.mac \
    test_strings_cond.mac \
    test_extroot.mac \
    test_z280_addr.mac \
    test_z280_double.mac \
    test_z280_io.mac \
    test_m80_compat.mac \
    test_m80_missing.mac \
    test_m80_numbers.mac
do
    run_rel_test "$t"
done

echo ""
echo "--- Binary output tests (assemble + link + compare) ---"

EXPECTED_32BIT="\
edc2edd2ede2edf2ddedc2ddede2fdede2fdedc20100fdedd20200ddedf20300ddedd20400fdedf20500\
edc3edd3ede3edf3ddedc3ddede3fdede3fdedc30100fdedd30200ddedf30300ddedd30400fdedf30500\
edcaeddaedeaedfaddedcaddedeafdedeafdedca0100fdedda0200ddedfa0300ddedda0400fdedfa0500\
edcbeddbedebedfbddedcbddedebfdedebfdedcb0100fdeddb0200ddedfb0300ddeddb0400fdedfb0500"

run_bin_test "test_z280_32bit.mac" "$EXPECTED_32BIT"

EXPECTED_SYSCTRL="\
ed710000ed710100ed71ffff\
ed55\
ed86dded8600dded8605fded8600fded86ff\
ed8edded8e00dded8e0afded8e00fded8e14\
ed96dded9600dded9605fded9600fded96ff\
ed9edded9e00dded9e0afded9e00fded9e14"

run_bin_test "test_z280_sysctrl.mac" "$EXPECTED_SYSCTRL"

EXPECTED_JAF="dd2803000000dd2003000000dd28fddd20fddd2800dd2000"
run_bin_test "test_z280_jaf_jar.mac" "$EXPECTED_JAF"

EXPECTED_MATH="\
edc6edd6ede6edf6ddede6fdede6ddedc6fdedc60500fdedd6f6ffddedf63200fdedf63412ddedd67856\
edceeddeedeeedfeddedeefdedeeddedcefdedce0500fdeddef6ffddedfe3200fdedfe3412ddedde7856\
03132333dd23fd23dd03fd030500fd13f6ffdd333200dd137856\
0b1b2b3bdd2bfd2bdd0bfd0b0500fd1bf6ffdd3b3200dd1b7856"

run_bin_test "test_z280_math.mac" "$EXPECTED_MATH"

EXPECTED_LDW="\
ed06ed16ed26ed36ed0eed1eed2eed3edded0605fded16fbdded2605fded36fbdded0e05fded1efbdded\
2e05fded3efbed0cdded14fded1ced0ddded15fded1ded040010dded240010fded040010ed050010dded\
250010fded050010ed2c0010dded340010fded3c0010ed2d0010dded350010fded3d0010ed4b0010dd2a\
0010ed530010fd220010dd010010dd3100010010dd1100100010010010dd010010f9ddf9"

run_bin_test "test_z280_ldw.mac" "$EXPECTED_LDW"

EXPECTED_MULT_DIV="\
edc0edc8edd0edd8ede0ede8edf8ddede0ddede8fdede0fdede8edf0ddedc8ddedd0ddedd8ddedf005fd\
edf005fdedc80010fdedd00010ddedc00010fdedc00010fdedd80010ddedf80010fdedf805edc1ddede1\
edf1ddedf105fdedc90010ddedf90010fdedf905edc4edccedd4eddcede4edecedfcddede4ddedecfded\
e4fdedecedf4ddedccddedd4ddeddcddedf405fdedf405fdedcc0010fdedd40010ddedc40010fdedc400\
10fdeddc0010ddedfc0010fdedfc05edc5ddede5edf5ddedf505fdedcd0010ddedfd0010fdedfd05"

run_bin_test "test_z280_mult_div.mac" "$EXPECTED_MULT_DIV"

echo ""
echo "--- Intel 8080 CPU tests ---"

run_bin_test "test_8080_noop.mac"    "002f3f37171febe3f9e9070fd8d0c8c0f8f0e8e0"
run_bin_test "test_8080_regs.mac"    "78797e773e42060036053c04343d05"
run_bin_test "test_8080_alu.mac"     "a7a6a8b1bae6ffee55f60fd601de02fe03"
run_bin_test "test_8080_rp.mac"      "0919293903132b3b01341211785621cdab3100000a1a0212"
run_bin_test "test_8080_jump.mac"    "c30001ca0002c20003da0004d20005fa0006f20007ea0008e20009c4000acc000bd4000cdc000dec000ee4000ffc00102200112a00123200133a0014"
run_bin_test "test_8080_pushpop.mac" "c5d5e5f5c1d1e1f1"
run_bin_test "test_8080_cputype.mac" "0007070f0f"
run_bin_test "test_8080_new.mac"     "c610ce207676"
run_bin_test "test_dc.mac"           "4142c3da4127c251"

echo ""
echo "--- SDCC output tests ---"
run_sdcc_basic_test
run_sdcc_reloc_test
run_sdcc_external_test
run_sdcc_byte_reloc_test

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
