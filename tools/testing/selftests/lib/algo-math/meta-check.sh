#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Meta-test for the differential harness: mutate the Rust side on purpose and
# check that the comparison FAILS. A harness that still reports "mismatches: 0"
# after a deliberate bug is worthless -- it would pass for any implementation,
# including the C one it is supposed to be independent of.
#
# Two mutations are tried, one per implementation:
#   int_sqrt: m >>= 2  ->  m >>= 1   (wrong radix, wrong results)
#   gcd:      return a << r.trailing_zeros()  ->  return a  (drops the factor)
#
# Each is applied to the tree source, the harness is regenerated and rebuilt,
# and the run must exit non-zero. The source is restored afterwards and the
# run must exit zero again, which also proves the restore was complete.

set -e

top=$(git rev-parse --show-toplevel 2>/dev/null || echo ../../../../../..)
isqrt=$top/rust/kernel/algo/int_sqrt.rs
gcd_rs=$top/rust/kernel/algo/gcd.rs
here=$(dirname "$0")

fail=0

run_case()
{
	name=$1
	file=$2
	from=$3
	to=$4

	cp "$file" "$file.meta-bak"

	# Apply exactly once; refuse to continue if the pattern is not there.
	if ! grep -qF "$from" "$file"; then
		echo "META $name: pattern not found, cannot mutate" >&2
		mv "$file.meta-bak" "$file"
		fail=1
		return
	fi
	sed -i "s|$from|$to|" "$file"

	rm -f "$here/math_rs.rs" "$here/libmathrs.a" "$here/diff"
	( cd "$here" && make >/dev/null 2>&1 )

	if timeout 120 "$here/diff" 200000 0x5eed >"$here/meta-$name.log" 2>&1; then
		echo "META $name: FAIL -- mutation did not change any result;" \
		     "the comparison is vacuous"
		fail=1
	else
		echo "META $name: ok -- mutation detected"
		grep -m1 'MISMATCH' "$here/meta-$name.log" || true
	fi

	# Restore and prove the restore is exact.
	mv "$file.meta-bak" "$file"
	rm -f "$here/math_rs.rs" "$here/libmathrs.a" "$here/diff"
	( cd "$here" && make >/dev/null 2>&1 )

	if ! timeout 120 "$here/diff" 200000 0x5eed >"$here/meta-$name-restored.log" 2>&1; then
		echo "META $name: FAIL -- still failing after restore"
		fail=1
	else
		echo "META $name: restored, clean run"
	fi
}

run_case int_sqrt "$isqrt" 'm >>= 2;' 'm >>= 1;'
run_case gcd "$gcd_rs" 'return a << r.trailing_zeros();' 'return a;'

if [ "$fail" -eq 0 ]; then
	echo "META RESULT: harness detects deliberate bugs"
else
	echo "META RESULT: FAILED"
fi
exit "$fail"
