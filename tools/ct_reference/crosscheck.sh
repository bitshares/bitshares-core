#!/bin/sh
# Prove in the Python reference and verify in C++, and the other way around.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
PY=${PY:-python3}
EXCHANGE=${EXCHANGE:-/tmp/ct_exchange}
REF="$ROOT/tools/ct_reference/reference.py"
test -x "$EXCHANGE"
"$PY" "$REF" self-test
"$PY" "$REF" prove-bp 1,2 3,9 | "$EXCHANGE" verify-bp
"$EXCHANGE" prove-bp 1,2 3,9 | "$PY" "$REF" verify-bp
"$PY" "$REF" prove-bp 0,1000,18446744073709551615 7,8,9 | "$EXCHANGE" verify-bp
"$PY" "$REF" prove-clsag | "$EXCHANGE" verify-clsag
"$EXCHANGE" prove-clsag | "$PY" "$REF" verify-clsag
echo "crosscheck ok"
