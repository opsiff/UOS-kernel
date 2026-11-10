// SPDX-License-Identifier: GPL-2.0

#include <linux/percpu.h>
#include <linux/preempt.h>

__rust_helper void rust_helper_preempt_disable(void)
{
	preempt_disable();
}

__rust_helper void rust_helper_preempt_enable(void)
{
	preempt_enable();
}

__rust_helper void *rust_helper_per_cpu_ptr(const void __percpu *ptr, unsigned int cpu)
{
	return per_cpu_ptr(ptr, cpu);
}

__rust_helper u32 rust_helper_this_cpu_read_u32(const u32 __percpu *ptr)
{
	return this_cpu_read(*ptr);
}

__rust_helper void rust_helper_this_cpu_write_u32(u32 __percpu *ptr, u32 val)
{
	this_cpu_write(*ptr, val);
}

__rust_helper void rust_helper_this_cpu_add_u32(u32 __percpu *ptr, u32 val)
{
	this_cpu_add(*ptr, val);
}

__rust_helper void rust_helper_this_cpu_inc_u32(u32 __percpu *ptr)
{
	this_cpu_inc(*ptr);
}

__rust_helper void rust_helper_this_cpu_dec_u32(u32 __percpu *ptr)
{
	this_cpu_dec(*ptr);
}

__rust_helper u32 rust_helper_this_cpu_inc_return_u32(u32 __percpu *ptr)
{
	return this_cpu_inc_return(*ptr);
}

__rust_helper u32 rust_helper_this_cpu_dec_return_u32(u32 __percpu *ptr)
{
	return this_cpu_dec_return(*ptr);
}

__rust_helper u32 rust_helper_this_cpu_add_return_u32(u32 __percpu *ptr, u32 val)
{
	return this_cpu_add_return(*ptr, val);
}

__rust_helper u64 rust_helper_this_cpu_read_u64(const u64 __percpu *ptr)
{
	return this_cpu_read(*ptr);
}

__rust_helper void rust_helper_this_cpu_write_u64(u64 __percpu *ptr, u64 val)
{
	this_cpu_write(*ptr, val);
}

__rust_helper void rust_helper_this_cpu_add_u64(u64 __percpu *ptr, u64 val)
{
	this_cpu_add(*ptr, val);
}

__rust_helper void rust_helper_this_cpu_inc_u64(u64 __percpu *ptr)
{
	this_cpu_inc(*ptr);
}

__rust_helper void rust_helper_this_cpu_dec_u64(u64 __percpu *ptr)
{
	this_cpu_dec(*ptr);
}

__rust_helper u64 rust_helper_this_cpu_inc_return_u64(u64 __percpu *ptr)
{
	return this_cpu_inc_return(*ptr);
}

__rust_helper u64 rust_helper_this_cpu_dec_return_u64(u64 __percpu *ptr)
{
	return this_cpu_dec_return(*ptr);
}

__rust_helper u64 rust_helper_this_cpu_add_return_u64(u64 __percpu *ptr, u64 val)
{
	return this_cpu_add_return(*ptr, val);
}
