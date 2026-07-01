#!/usr/bin/env bash
# Test runner for HolyC++.
#
# For every example under examples/, checks that:
#   1. bootstrap/hcrun produces exactly the golden output recorded
#      under tests/golden/.
#   2. selfhost/compiler.hc++, itself executed by hcrun, produces the
#      exact same output when interpreting the same example. This is
#      what proves the self hosted toolchain in selfhost/ genuinely
#      implements the same language hcrun does.
#
# Usage: tests/run_tests.sh (run from the project root)

set -u
cd "$(dirname "$0")/.."

RUNTIME=bootstrap/hcrun
COMPILER=selfhost/compiler.hc++
GOLDEN_DIR=tests/golden

if [ ! -x "$RUNTIME" ]; then
    echo "building $RUNTIME ..."
    make build || exit 1
fi

pass=0
fail=0

for source in examples/*.hc++; do
    name=$(basename "$source" .hc++)
    golden="$GOLDEN_DIR/$name.txt"

    if [ ! -f "$golden" ]; then
        echo "SKIP  $name (no golden file at $golden)"
        continue
    fi

    direct_output=$("$RUNTIME" "$source" 2>&1)
    expected=$(cat "$golden")

    if [ "$direct_output" != "$expected" ]; then
        echo "FAIL  $name (hcrun output does not match tests/golden/$name.txt)"
        fail=$((fail + 1))
        continue
    fi

    selfhost_output=$("$RUNTIME" "$COMPILER" "$source" 2>&1)

    if [ "$selfhost_output" != "$expected" ]; then
        echo "FAIL  $name (self hosted compiler output does not match hcrun)"
        fail=$((fail + 1))
        continue
    fi

    echo "PASS  $name"
    pass=$((pass + 1))
done

echo
echo "$pass passed, $fail failed"

if [ "$fail" -ne 0 ]; then
    exit 1
fi
