// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int __cond_resched(void) { return 0; }
typedef int (*cmp_r_func_t)(const void *, const void *, const void *);
typedef void (*swap_r_func_t)(void *, void *, int, const void *);

extern void sort_r_c(void *, size_t, size_t, cmp_r_func_t, swap_r_func_t, const void *);
extern void sort_r_rs(void *, size_t, size_t, cmp_r_func_t, swap_r_func_t, const void *);

struct ctx { uint64_t swaps; uint64_t magic; };

static int cmp_priv(const void *a, const void *b, const void *priv)
{
	const struct ctx *c = priv;
	uint64_t x = *(const uint64_t *)a ^ c->magic, y = *(const uint64_t *)b ^ c->magic;

	return (x > y) - (x < y);
}

static void swap_priv(void *a, void *b, int size, const void *priv)
{
	struct ctx *c = (struct ctx *)priv;
	unsigned char *pa = a, *pb = b;

	for (int i = 0; i < size; i++) {
		unsigned char t = pa[i];

		pa[i] = pb[i];
		pb[i] = t;
	}
	c->swaps++;
}

int main(void)
{
	size_t n = 20000;
	uint64_t *src = malloc(n * 8), *x = malloc(n * 8), *y = malloc(n * 8);
	struct ctx c1 = { 0, 0x5a5a5a5aULL }, c2 = { 0, 0x5a5a5a5aULL };

	srand(7);
	for (size_t i = 0; i < n; i++) {
		/* Not random() coherent, but only needs to be reproducible. */
		src[i] = ((uint64_t)rand() << 32) ^ (uint64_t)rand();
	}
	memcpy(x, src, n * 8); memcpy(y, src, n * 8);
	sort_r_c(x, n, 8, cmp_priv, swap_priv, &c1);
	sort_r_rs(y, n, 8, cmp_priv, swap_priv, &c2);
	if (memcmp(x, y, n * 8)) {
		printf("DATA MISMATCH\n");
		return 1;
	}
	printf("swap-callback path: data identical, swaps C=%llu Rust=%llu -> %s\n",
	       (unsigned long long)c1.swaps, (unsigned long long)c2.swaps,
	       c1.swaps == c2.swaps ? "identical" : "DIFFERENT");
	return 0;
}
