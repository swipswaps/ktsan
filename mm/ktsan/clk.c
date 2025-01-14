// SPDX-License-Identifier: GPL-2.0
#include "ktsan.h"

#include <linux/kernel.h>

void kt_clk_init(kt_clk_t *clk)
{
	memset(clk, 0, sizeof(*clk));
}

void kt_clk_acquire(kt_clk_t *dst, kt_clk_t *src)
{
	int i;

	for (i = 0; i < KT_MAX_THREAD_COUNT; i++)
		dst->time[i] = max(dst->time[i], src->time[i]);
}

void kt_clk_set(kt_clk_t *dst, kt_clk_t *src)
{
	int i;

	for (i = 0; i < KT_MAX_THREAD_COUNT; i++)
		dst->time[i] = src->time[i];
}
