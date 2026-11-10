# SPDX-License-Identifier: GPL-2.0
# Differential test: the Rust MS-DOS parser against the kernel's own parser.
#
# The harness links the in-tree rust_msdos parser source, so it exercises the
# exact code the kernel builds. Needs root (loop devices) and a set of images;
# without them the run skips instead of failing.

TEST_PROGS := run.sh

include ../../lib.mk
