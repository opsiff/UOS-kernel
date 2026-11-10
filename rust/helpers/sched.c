// SPDX-License-Identifier: GPL-2.0

#include <linux/sched.h>

__rust_helper int rust_helper_cond_resched(void)
{
	return cond_resched();
}
