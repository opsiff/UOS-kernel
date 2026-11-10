# SPDX-License-Identifier: GPL-2.0
#
# Differential test between the C and the Rust implementation of
# int_sqrt(), lcm() and lcm_not_zero().
# Transitional: removed together with the C halves of lib/math/*.

CLANG	?= clang
RUSTC	?= rustc
CFLAGS	:= -O2 -Wall
LDLIBS	:= -lpthread -ldl -lm

all: diff

# The generated files must depend on the sources they are generated FROM, not
# only on the generator: otherwise editing a kernel source leaves a stale copy
# behind and the comparison silently tests the old code. (Found by
# meta-check.sh: after restoring a deliberate mutation the stale copy made the
# run fail, i.e. the harness was comparing against the mutated build.)
top	= ../../../../..
math_c_orig.c: mk-math-c.sh $(top)/lib/math/gcd.c $(top)/lib/math/lcm.c \
	       $(top)/lib/math/int_sqrt.c
	./mk-math-c.sh

math_rs.rs: mk-math-rs.sh $(top)/rust/kernel/algo/gcd.rs \
	    $(top)/rust/kernel/algo/int_sqrt.rs $(top)/rust/kernel/algo/lcm.rs
	./mk-math-rs.sh

math_c.o: math_c_orig.c
	$(CLANG) $(CFLAGS) -c $< -o $@

libmathrs.a: math_rs.rs
	$(RUSTC) --edition 2021 -O --crate-type staticlib $< -o $@

diff: main.c math_c.o libmathrs.a
	$(CLANG) $(CFLAGS) main.c math_c.o libmathrs.a $(LDLIBS) -o $@

# Convenience: the run recorded in the evidence file.
run: diff
	./diff 1000000 0x9e3779b97f4a7c15

clean:
	rm -f math_c_orig.c math_rs.rs math_c.o libmathrs.a diff

.PHONY: all run clean
