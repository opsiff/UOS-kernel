// SPDX-License-Identifier: GPL-2.0
//
// Differential test: the C and the Rust implementations of int_sqrt(), lcm()
// and lcm_not_zero() are fed the same inputs and must agree bit for bit.
//
// Transitional: removed together with the C halves of the lib/math files.

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "algo-math.h"

static unsigned long long rng_state;

/* xorshift64*: small, deterministic, and seedable so a run is reproducible. */
static uint64_t rnd(void)
{
	uint64_t x = rng_state;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	rng_state = x;

	return x * 0x2545f4914f6cdd1dULL;
}

static unsigned long long mismatches;

static void check_sqrt(ulong x)
{
	ulong c = int_sqrt(x);
	ulong r = rs_int_sqrt(x);

	if (c != r) {
		if (mismatches < 10)
			printf("MISMATCH int_sqrt(x=%lu): C=%lu Rust=%lu\n", x, c, r);
		mismatches++;
	}
}

static void check_gcd(ulong a, ulong b)
{
	ulong c = gcd(a, b);
	ulong r = rs_gcd(a, b);

	if (c != r) {
		if (mismatches < 10)
			printf("MISMATCH gcd(%lu, %lu): C=%lu Rust=%lu\n", a, b, c, r);
		mismatches++;
	}
}

static void check_lcm(ulong a, ulong b)
{
	ulong c = lcm(a, b);
	ulong r = rs_lcm(a, b);
	ulong cn = lcm_not_zero(a, b);
	ulong rn = rs_lcm_not_zero(a, b);

	if (c != r) {
		if (mismatches < 10)
			printf("MISMATCH lcm(%lu, %lu): C=%lu Rust=%lu\n", a, b, c, r);
		mismatches++;
	}

	if (cn != rn) {
		if (mismatches < 10)
			printf("MISMATCH lcm_not_zero(%lu, %lu): C=%lu Rust=%lu\n",
			       a, b, cn, rn);
		mismatches++;
	}
}

int main(int argc, char **argv)
{
	static const ulong sqrt_edges[] = {
		0, 1, 2, 3, 4, 5, 8, 9, 15, 16, 17,
		255, 256, 257,
		0xffffffffUL, 0x100000000UL, 0x100000001UL,
		0x7fffffffffffffffUL, 0x8000000000000000UL,
		0xfffffffffffffffeUL, 0xffffffffffffffffUL,
	};
	static const ulong lcm_edges[][2] = {
		{ 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 },
		{ 0, 0xffffffffffffffffUL },
		{ 2, 4 }, { 8, 12 }, { 21, 6 }, { 7, 13 },
		{ 0xffffffffffffffffUL, 0xfffffffffffffffdUL }, /* coprime, overflows */
		{ 0x8000000000000000UL, 0x8000000000000000UL },
		{ 0xffffffffffffffffUL, 0xffffffffffffffffUL },
	};
	unsigned long long n = 1000000ULL;
	unsigned long long i;
	unsigned int e;

	if (argc > 1)
		n = strtoull(argv[1], NULL, 0);
	rng_state = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0x9e3779b97f4a7c15ULL;

	printf("int_sqrt/lcm differential: %llu random pairs, seed 0x%016llx\n",
	       n, rng_state);

	for (e = 0; e < ARRAY_SIZE(sqrt_edges); e++)
		check_sqrt(sqrt_edges[e]);

	for (e = 0; e < ARRAY_SIZE(lcm_edges); e++) {
		check_gcd(lcm_edges[e][0], lcm_edges[e][1]);
		check_lcm(lcm_edges[e][0], lcm_edges[e][1]);
	}

	for (i = 0; i < n; i++) {
		ulong x = (ulong)rnd();
		ulong a = (ulong)rnd();
		ulong b = (ulong)rnd();
		/* Mix in small values so the common cases are covered too. */
		if ((i & 3) == 0) {
			a &= 0xffff;
			b &= 0xffff;
		}

		check_sqrt(x);
		check_gcd(a, b);
		check_lcm(a, b);
	}

	printf("comparisons: gcd=%llu int_sqrt=%llu lcm=%llu lcm_not_zero=%llu\n",
	       n + ARRAY_SIZE(lcm_edges),
	       n + ARRAY_SIZE(sqrt_edges), n + ARRAY_SIZE(lcm_edges),
	       n + ARRAY_SIZE(lcm_edges));
	printf("mismatches: %llu\n", mismatches);

	return mismatches ? 1 : 0;
}
