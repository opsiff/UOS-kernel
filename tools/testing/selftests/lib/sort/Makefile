# SPDX-License-Identifier: GPL-2.0
#
# Differential test between the C and the Rust implementation of lib/sort.
# Transitional: removed together with the C half of lib/sort.c.
#
# Pin the toolchain (README.md records what the published numbers were taken
# with); a different compiler can change the comparison counts only if it
# rewrites the loops, but keep it fixed to compare numbers across runs.

CLANG	?= clang-21
RUSTC	?= rustc
CFLAGS	:= -O2 -I inc
LDLIBS	:= -lpthread -ldl -lm

all: bench swap_test

sort_c.o: sort_c_orig.c
	$(CLANG) $(CFLAGS) -c sort_c_orig.c \
		-Dsort_r=sort_r_c -Dsort=sort_c \
		-Dsort_nonatomic=sort_nonatomic_c \
		-Dsort_r_nonatomic=sort_r_nonatomic_c -o $@

stub.o: stub.c
	$(CLANG) $(CFLAGS) -c $< -o $@

sort_rs.rs: ../../../../../rust/kernel/algo/sort.rs \
	    ../../../../../rust/kernel/sort.rs mk-sort-rs.sh
	./mk-sort-rs.sh

libsortrs.a: sort_rs.rs
	$(RUSTC) --edition 2021 -O --crate-type staticlib \
		--cfg CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS $< -o $@

bench: main.c sort_c.o stub.o libsortrs.a
	$(CLANG) $(CFLAGS) main.c sort_c.o stub.o libsortrs.a $(LDLIBS) -o $@

# swap_test.c provides its own __cond_resched(), hence no stub.o here.
swap_test: swap_test.c sort_c.o libsortrs.a
	$(CLANG) $(CFLAGS) swap_test.c sort_c.o libsortrs.a $(LDLIBS) -o $@

clean:
	rm -f sort_c.o stub.o libsortrs.a sort_rs.rs bench swap_test

.PHONY: all clean
