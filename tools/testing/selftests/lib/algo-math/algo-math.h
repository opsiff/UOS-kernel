/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Prototypes shared by the differential driver and the generated C reference.
 */

#ifndef ALGO_MATH_H
#define ALGO_MATH_H

typedef unsigned long ulong;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* Reference side: generated from the kernel sources by mk-math-c.sh. */
ulong gcd(ulong a, ulong b);
ulong lcm(ulong a, ulong b);
ulong lcm_not_zero(ulong a, ulong b);
ulong int_sqrt(ulong x);

/* Rust side: generated from the rust/kernel/algo sources by mk-math-rs.sh. */
ulong rs_gcd(ulong a, ulong b);
ulong rs_int_sqrt(ulong x);
ulong rs_lcm(ulong a, ulong b);
ulong rs_lcm_not_zero(ulong a, ulong b);

#endif /* ALGO_MATH_H */
