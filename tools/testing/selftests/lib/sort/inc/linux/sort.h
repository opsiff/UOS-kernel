/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BENCH_SORT_H
#define _BENCH_SORT_H
typedef int (*cmp_func_t)(const void *, const void *);
typedef int (*cmp_r_func_t)(const void *, const void *, const void *);
typedef void (*swap_func_t)(void *, void *, int);
typedef void (*swap_r_func_t)(void *, void *, int, const void *);
#endif
