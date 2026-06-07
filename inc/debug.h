/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021, Fuzhou Rockchip Electronics Co., Ltd
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <linux/printk.h>

/* runtime-selectable debug topics, module parameter "debug_mask" */
enum rk915_debug_mask {
	RK915_DBG_SCAN		= BIT(1),
	RK915_DBG_ROC		= BIT(2),
	RK915_DBG_TX		= BIT(3),
	RK915_DBG_MAIN		= BIT(4),
	RK915_DBG_IF		= BIT(5),
	RK915_DBG_UMACIF	= BIT(6),
	RK915_DBG_RX		= BIT(7),
	RK915_DBG_HAL		= BIT(8),
	RK915_DBG_CRYPTO	= BIT(9),
	RK915_DBG_DUMP_RX	= BIT(10),
	RK915_DBG_DUMP_HAL	= BIT(11),
	RK915_DBG_P2P		= BIT(13),
	RK915_DBG_VIF		= BIT(14),
	RK915_DBG_DUMP_TX	= BIT(15),
	RK915_DBG_SDIO		= BIT(16),
	RK915_DBG_FIRMWARE	= BIT(17),
	RK915_DBG_HALIO		= BIT(18),
	RK915_DBG_DAPT		= BIT(19),
	RK915_DBG_RECOVERY	= BIT(20),
};

extern unsigned int rk915_debug_mask;

#define rk915_err(fmt, ...) \
	pr_err("rk915: " fmt, ##__VA_ARGS__)

#define rk915_info(fmt, ...) \
	pr_info("rk915: " fmt, ##__VA_ARGS__)

#define rk915_dbg(mask, fmt, ...)					\
do {									\
	if (rk915_debug_mask & (mask))					\
		printk(KERN_DEBUG "rk915: " fmt, ##__VA_ARGS__);	\
} while (0)

#define rk915_dbg_dump(mask, ...)					\
do {									\
	if (rk915_debug_mask & (mask))					\
		print_hex_dump(KERN_DEBUG, __VA_ARGS__);		\
} while (0)

#define DUMP_HAL (rk915_debug_mask & RK915_DBG_DUMP_HAL)
#define DUMP_TX  (rk915_debug_mask & RK915_DBG_DUMP_TX)
#define DUMP_RX  (rk915_debug_mask & RK915_DBG_DUMP_RX)

#define VIF_INDEX_TO_INTERFACE_NAME(x)					\
	(((x) == 0) ? "p2p0" : "wlan0")

void convert_cmd_to_str(int id, char *str, size_t len);
void convert_event_to_str(int id, char *str, size_t len);

#endif /* _DEBUG_H_ */
