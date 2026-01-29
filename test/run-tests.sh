#!/bin/bash
# Pith test runner
# Each test file should have comments at the top: # expect: <expected_output>
# Multiple expect lines are supported for multi-line output

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PITH="${SCRIPT_DIR}/../pith"

passed=0
failed=0
total=0

for f in "$SCRIPT_DIR"/*.pith; do
    [ -f "$f" ] || continue
    total=$((total + 1))

    # Extract all expected lines
    expected=$(grep '^# expect: ' "$f" | sed 's/^# expect: //')

    # Run the test
    output=$("$PITH" "$f" 2>&1)

    # Compare
    if [ "$output" = "$expected" ]; then
        echo "PASS: $(basename "$f")"
        passed=$((passed + 1))
    else
        echo "FAIL: $(basename "$f")"
        echo "  Expected:"
        echo "$expected" | sed 's/^/    /'
        echo "  Got:"
        echo "$output" | sed 's/^/    /'
        failed=$((failed + 1))
    fi
done

echo ""
echo "Results: $passed/$total passed, $failed failed"

if [ $failed -gt 0 ]; then
    exit 1
fi
