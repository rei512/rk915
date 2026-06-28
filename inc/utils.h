/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021, Fuzhou Rockchip Electronics Co., Ltd
 */

#ifndef _UTILS_H
#define _UTILS_H

#include "core.h"
#define MASK_BITS(msb, lsb) (((1U << ((msb) - (lsb) + 1)) - 1) \
				<< (lsb))

#define EXTRACT_BITS(arg, msb, lsb) ((arg & MASK_BITS(msb, lsb)) >> (lsb))

#define INSERT_BITS(arg, msb, lsb, value) ((arg) = ((arg) &		     \
							~MASK_BITS(msb, lsb)) |  \
							(((value) << (lsb)) &    \
							MASK_BITS(msb, lsb)))

#define FRAME_CTRL_TYPE(arg) EXTRACT_BITS(arg, 3, 2)
#define FRAME_CTRL_STYPE(arg) EXTRACT_BITS(arg, 7, 4)
#define FTYPE_DATA 0x02
#define FSTYPE_QOS_DATA 0x08

struct hal_priv;
void update_aux_adc_voltage(struct img_priv *priv,
				   unsigned char pdout);
void rk915_wake_waiters(struct hal_priv *hal);
int conv_str_to_byte(unsigned char *byte,
			unsigned char *str,
			int len);
extern int wait_for_scan_abort(struct img_priv *priv);
extern int wait_for_scan_complete(struct img_priv *priv);
extern int wait_for_cancel_hw_roc(struct img_priv *priv);
extern int wait_for_channel_prog_complete(struct img_priv *priv);
extern int wait_for_tx_queue_flush_complete(struct img_priv *priv,
						unsigned int token);
int wait_for_fw_error_process_complete(struct img_priv *priv);
int wait_for_pm_resume_done(struct hal_priv *hal);
int wait_for_rxq_empty(struct hal_priv *hal);
int wait_for_fw_error_cmd_done(struct img_priv *priv);
int rk915_wait_fw_ready_to_sleep(struct hal_priv *hal);
int find_main_iface(struct img_priv *priv);
int find_p2p_iface(struct img_priv *priv);
bool is_main_iface(u8 *if_addr);

/*
 * IEEE 802.11 address fields:
 * ToDS FromDS Addr1 Addr2 Addr3 Addr4
 *   0        0        DA    SA    BSSID n/a
 *   0        1        DA    BSSID SA    n/a
 *   1        0        BSSID SA    DA    n/a
 *   1        1        RA    TA    DA    SA
 */
static inline u8 *ieee80211_get_BSSID(struct ieee80211_hdr *hdr)
{
	if (ieee80211_has_a4(hdr->frame_control))
		return NULL;
	if (ieee80211_has_fromds(hdr->frame_control))
		return hdr->addr2;
	if (ieee80211_has_tods(hdr->frame_control))
		return hdr->addr1;
	return hdr->addr3;
}

#endif /* _UTILS_H */

/* EOF */
