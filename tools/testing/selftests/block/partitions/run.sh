#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Usage: PARTS_IMGS=<dir with *.img> ./run.sh
#
# Builds the userspace harness from the in-tree parser source and compares it
# against the running kernel's msdos parser via loop devices.

set -eu

if [ -z "${PARTS_IMGS:-}" ]; then
	echo "SKIP: set PARTS_IMGS to a directory of partition images (see gen.py)"
	exit 4
fi

if [ "$(id -u)" -ne 0 ]; then
	echo "SKIP: needs root for loop devices"
	exit 4
fi

here=$(dirname "$0")
rustc --edition 2021 -O "$here/harness.rs" -o "$here/harness"
PARTS_IMGS="$PARTS_IMGS" python3 "$here/diff.py"
