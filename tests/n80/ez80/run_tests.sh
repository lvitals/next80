#!/bin/bash
# Test script for eZ80 support in n80

N80=../../../n80/n80

echo "Running eZ80 tests..."

$N80 test_ez80.asm test_ez80.rel --build-type sdcc

if [ $? -eq 0 ]; then
    echo "eZ80 assembly test PASSED"
else
    echo "eZ80 assembly test FAILED"
    exit 1
fi

# TODO: Add lk80 tests once it's fully verified
