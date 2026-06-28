// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021, Fuzhou Rockchip Electronics Co., Ltd
 */

#include "core.h"
#include "if_io.h"
#include "utils.h"

void rk915_wake_waiters(struct hal_priv *hal)
{
	if (hal)
		wake_up_all(&hal->wait_q);
}

int conv_str_to_byte(unsigned char *byte,
			unsigned char *str,
			int len)
{
	int  i, j = 0;
	unsigned char ch, val = 0;

	for (i = 0; i < (len * 2); i++) {
		/*convert to lower*/
		ch = ((str[i] >= 'A' && str[i] <= 'Z') ? str[i] + 32 : str[i]);

		if ((ch < '0' || ch > '9') && (ch < 'a' || ch > 'f'))
			return -1;

		if (ch >= '0' && ch <= '9')  /*check is digit*/
			ch = ch - '0';
		else
			ch = ch - 'a' + 10;

		val += ch;

		if (!(i%2))
			val <<= 4;
		else {
			byte[j] = val;
			j++;
			val = 0;
		}
	}

	return 0;
}

int wait_for_scan_abort(struct img_priv *priv)
{
	wait_event_timeout(priv->hal->wait_q,
			   priv->hal->fw_error || priv->scan_abort_done,
			   SCAN_ABORT_TIMEOUT_TICKS);

	if (!priv->scan_abort_done) {
		rk915_err("%s-UMAC: No SCAN_ABORT_DONE after %ld ticks\n",
			   priv->name, SCAN_ABORT_TIMEOUT_TICKS);
		return 0;
	}

	rk915_dbg(RK915_DBG_SCAN, "%s-UMAC: Scan abort complete\n", priv->name);

	return 0;
}

int wait_for_scan_complete(struct img_priv *priv)
{
	wait_event_timeout(priv->hal->wait_q,
			   priv->hal->fw_error ||
			   priv->params->hw_scan_status == HW_SCAN_STATUS_NONE,
			   msecs_to_jiffies(5000));

	if (priv->params->hw_scan_status != HW_SCAN_STATUS_NONE) {
		rk915_err("%s-UMAC: No Scan complete after %ld ticks\n",
			   priv->name, msecs_to_jiffies(5000));
		return 0;
	}

	rk915_dbg(RK915_DBG_SCAN, "%s-UMAC: Scan complete\n", priv->name);

	return 0;
}

int wait_for_cancel_hw_roc(struct img_priv *priv)
{
	wait_event_timeout(priv->hal->wait_q,
			   priv->hal->fw_error || priv->cancel_hw_roc_done,
			   CANCEL_HW_ROC_TIMEOUT_TICKS);

	if (!priv->cancel_hw_roc_done) {
		rk915_err("%s-UMAC: Warning: Didn't get CANCEL_HW_ROC_DONE after %ld timer ticks\n",
			priv->name,
			CANCEL_HW_ROC_TIMEOUT_TICKS);
		if (priv->hal->fw_error_processing)
			return 0;
		return -1;
	}

	rk915_dbg(RK915_DBG_ROC, "%s-UMAC: Cancel HW RoC complete\n", priv->name);

	return 0;
}

int wait_for_channel_prog_complete(struct img_priv *priv)
{
	if (priv->hal->during_pm_resume)
		return 0;

	wait_event_timeout(priv->hal->wait_q,
			   priv->hal->fw_error || priv->chan_prog_done,
			   CH_PROG_TIMEOUT_TICKS);

	if (!priv->chan_prog_done) {
		rk915_err("%s-UMAC: No channel prog done after %ld ticks\n",
			   priv->name, CH_PROG_TIMEOUT_TICKS);
		return -1;
	}

	rk915_dbg(RK915_DBG_UMACIF, "%s-UMAC: Channel Prog Complete\n", priv->name);

	return 0;
}


int wait_for_reset_complete(struct img_priv *priv, int enable)
{
	int timeout;

	if (enable)
		timeout = RESET_TIMEOUT_TICKS;
	else
		timeout = msecs_to_jiffies(3000);

	wait_event_timeout(priv->hal->wait_q, priv->reset_complete, timeout);

	if (!priv->reset_complete) {
		rk915_err("%s-UMAC: No reset complete after %d ticks\n",
			   priv->name, timeout);
		if (!enable) {
			/* The firmware stops servicing the bus as soon as it
			 * disables the LMAC, so this completion may never
			 * arrive. The chip is power cycled before the next
			 * bring-up, so do not treat it as an error.
			 */
			return 0;
		}
		rk915_signal_io_error(priv->hal, FW_ERR_RESET_CMD);
		wait_for_fw_error_cmd_done(priv);
		return -1;
	}

	rk915_dbg(RK915_DBG_MAIN, "%s-UMAC: Reset complete\n", priv->name);
	return 0;
}

int wait_for_read_csr_cmp(struct img_priv *priv)
{
	wait_event_timeout(priv->hal->wait_q,
			   priv->hal->fw_error || priv->read_csr_complete,
			   msecs_to_jiffies(1000));

	if (!priv->read_csr_complete) {
		rk915_err("%s-UMAC: No read_csr_complete after %ld ticks\n",
			   priv->name, msecs_to_jiffies(1000));
		return 0;
	}

	rk915_dbg(RK915_DBG_SCAN, "%s-UMAC: read_csr_complete\n", priv->name);

	return 0;
}

int wait_for_fw_error_process_complete(struct img_priv *priv)
{
	wait_event_timeout(priv->hal->wait_q, !priv->hal->fw_error_processing,
			   FW_ERR_PROCESS_TIMEOUT_TICKS);

	if (priv->hal->fw_error_processing) {
		rk915_err("%s-UMAC: No fw_error_process complete after %ld ticks\n",
			   priv->name, FW_ERR_PROCESS_TIMEOUT_TICKS);
		return -1;
	}

	rk915_dbg(RK915_DBG_UMACIF, "%s-UMAC: fw_error_process complete\n", priv->name);

	return 0;
}

int wait_for_fw_error_cmd_done(struct img_priv *priv)
{
	wait_event_timeout(priv->hal->wait_q, priv->hal->fw_error_cmd_done,
			   msecs_to_jiffies(1000));

	if (!priv->hal->fw_error_cmd_done) {
		rk915_err("No fw_error_cmd done after %ld ticks\n",
			   msecs_to_jiffies(1000));
		return -1;
	}

	rk915_dbg(RK915_DBG_UMACIF, "fw_error_cmd_done\n");

	return 0;
}

int wait_for_pm_resume_done(struct hal_priv *hal)
{
	wait_event_timeout(hal->wait_q, !hal->during_pm_resume,
			   msecs_to_jiffies(1000));

	if (hal->during_pm_resume) {
		rk915_err("No pm_resume done after %ld ticks\n",
			   msecs_to_jiffies(1000));
		return -1;
	}

	rk915_dbg(RK915_DBG_UMACIF, "pm_resume done\n");

	return 0;
}

int wait_for_rxq_empty(struct hal_priv *hal)
{
	wait_event_timeout(hal->wait_q, skb_queue_len(&hal->rxq) == 0,
			   RXQ_EMPTY_TIMEOUT_TICKS);

	if (skb_queue_len(&hal->rxq) > 0) {
		rk915_err("rxq not empty after %ld ticks\n",
			   RXQ_EMPTY_TIMEOUT_TICKS);
		return -1;
	}

	rk915_dbg(RK915_DBG_RECOVERY, "%s complete\n", __func__);

	return 0;
}

void update_aux_adc_voltage(struct img_priv *priv,
				   unsigned char pdout)
{
	static unsigned int index;

	if (index >= MAX_AUX_ADC_SAMPLES)
		index = 0;

	priv->params->pdout_voltage[index++] = pdout;
}

/*
 * find iterface index of main interface of wlan0
 */
int find_main_iface(struct img_priv *priv)
{
	int i, index = MAX_VIFS;

	for (i = 0; i < MAX_VIFS; i++) {
		if (priv->vifs[i] &&
			ether_addr_equal(priv->vifs[i]->addr, vif_macs[0])) {
			index = i;
			break;
		}
	}
	return index;
}

/*
 * find iterface index of main interface of p2p0
 */
int find_p2p_iface(struct img_priv *priv)
{
	int i, index = MAX_VIFS;

	for (i = 0; i < MAX_VIFS; i++) {
		if (priv->vifs[i] &&
			ether_addr_equal(priv->vifs[i]->addr, vif_macs[1])) {
			index = i;
			break;
		}
	}
	return index;
}

/*
 * is main interface wlan0
 */
bool is_main_iface(u8 *if_addr)
{
	if (ether_addr_equal(if_addr, vif_macs[0]))
		return true;
	return false;
}
