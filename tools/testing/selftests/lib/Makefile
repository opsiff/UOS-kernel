# SPDX-License-Identifier: GPL-2.0-only
# Makefile for lib/ function selftests

# sort/ is a differential harness between the C and the Rust implementation of
# lib/sort. It has its own Makefile and is not a kselftest case, so it is built
# and cleaned here instead of being listed in TEST_PROGS.
all: sort-tools

clean: sort-clean

sort-tools:
	$(MAKE) -C sort

sort-clean:
	$(MAKE) -C sort clean

.PHONY: sort-tools sort-clean

TEST_PROGS := bitmap.sh
include ../lib.mk
