// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#include <stdint.h>

typedef int (*cmp_r_func_t)(const void *, const void *, const void *);
typedef void (*swap_r_func_t)(void *, void *, int, const void *);

extern void sort_r_c(void *base, size_t num, size_t size, cmp_r_func_t, swap_r_func_t, const void *);
extern void sort_r_rs(void *base, size_t num, size_t size, cmp_r_func_t, swap_r_func_t, const void *);

static unsigned long long ncmp;

static int cmp64(const void *a, const void *b, const void *priv)
{
	(void)priv;
	ncmp++;
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return (x > y) - (x < y);
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void fill(uint64_t *v, size_t n, int pattern, unsigned seed)
{
	srand(seed);
	for (size_t i = 0; i < n; i++)
		v[i] = pattern == 0 ? ((uint64_t)rand() << 32) ^ (uint64_t)rand() : i;
	if (pattern == 2) {
		for (int k = 0; k < (int)(n / 100); k++) {
			size_t i = rand() % n, j = rand() % n;
			uint64_t t = v[i]; v[i] = v[j]; v[j] = t;
		}
	}
}

static int run(const char *name, void (*fn)(void *, size_t, size_t, cmp_r_func_t, swap_r_func_t, const void *),
	       uint64_t *src, size_t n, uint64_t *out, unsigned long long *cmps)
{
	memcpy(out, src, n * sizeof(uint64_t));
	ncmp = 0;
	double t0 = now();

	fn(out, n, sizeof(uint64_t), cmp64, NULL, NULL);
	double t1 = now();
	*cmps = ncmp;
	printf("%-10s n=%-8zu %8.1f ms  %12llu cmp  %6.2f cmp/el\n", name, n, (t1 - t0) * 1e3, ncmp,
	       (double)ncmp / n);
	return 0;
}

int main(void)
{
	static const size_t sizes[] = { 1000, 100000, 1000000 };
	static const char *const pname[] = { "random", "sorted", "nearly" };

	printf("%-6s %-10s %-12s %8s %14s %9s\n", "impl", "pattern", "n",
	       "time", "comparisons", "cmp/el");
	for (int p = 0; p < 3; p++) {
		for (unsigned int s = 0; s < ARRAY_SIZE(sizes); s++) {
			size_t n = sizes[s];
			uint64_t *src = malloc(n * sizeof(uint64_t));
			uint64_t *a = malloc(n * sizeof(uint64_t));
			uint64_t *b = malloc(n * sizeof(uint64_t));
			unsigned long long c1, c2;

			fill(src, n, p, 12345 + p);
			printf("-- %s --\n", pname[p]);
			run("C", sort_r_c, src, n, a, &c1);
			run("Rust", sort_r_rs, src, n, b, &c2);
			if (memcmp(a, b, n * sizeof(uint64_t)) != 0) {
				printf("MISMATCH at %s n=%zu\n", pname[p], n);
				return 1;
			}
			if (c1 != c2) {
				printf("  NOTE: comparison counts differ: C=%llu Rust=%llu\n",
				       c1, c2);
			}
			free(src); free(a); free(b);
		}
	}
	printf("all outputs identical\n");
	return 0;
}
