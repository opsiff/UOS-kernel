# SPDX-License-Identifier: GPL-2.0
# Delegate to the per-partition-parser selftests.

TARGETS := partitions

all clean install:
	@for t in $(TARGETS); do $(MAKE) -C $$t $@ || exit $$?; done

.PHONY: all clean install
