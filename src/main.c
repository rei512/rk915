// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/debugfs.h>
#include "core.h"
#include "hal_common.h"
#include "hal_io.h"
#include "if_io.h"
#include "utils.h"

static int print_version = 1;

int _rpu_umac_if_init(struct hal_priv *priv)
{
	struct wifi_dev *wdev;
	int error;

	wdev = proc_init(priv);
	if (!wdev)
		return -ENOMEM;

	error = rpu_init(wdev);
	if (error) {
		proc_exit(wdev);
		return error;
	}

	rk915_debugfs_init(wdev);

	return 0;
}

void _rpu_umac_if_exit(struct hal_priv *priv)
{
	rpu_exit(priv->wifi);
}

static int rpu_lmac_feature_init(void)
{
	return 0;
}

int rpu_core_init(struct img_priv *priv)
{
	int ret = 0;
	unsigned int reset_type = LMAC_ENABLE;

	if (priv->state == STARTED)
		return ret;

	rk915_dbg(RK915_DBG_MAIN, "%s-UMAC: Init called\n", priv->name);
	spin_lock_init(&tsf_lock);
	rpu_if_init(priv, priv->name);

	/* Enable the LMAC, set defaults and initialize TX */
	priv->reset_complete = 0;

	reset_type |= priv->params->rpu_sleep_type;

	rk915_dbg(RK915_DBG_MAIN, "%s-UMAC: Reset (ENABLE) reset_type %x\n", priv->name, reset_type);

	if (hal_ops.init_bufs(priv->hal, NUM_TX_DESCS,
				NUM_RX_BUFS_2K,
				NUM_RX_BUFS_12K,
				priv->params->max_data_size) < 0) {
		ret = -1;
		rk915_err("%s: init_bufs failed\n", __func__);
		goto hal_stop;
	}

	if (hal_ops.start(priv->hal)) {
		ret = -1;
		rk915_err("%s: hal_ops.start failed\n", __func__);
		goto rpu_if_deinit;
	}

	/* notify fw wakeup */
	rk915_notify_pm(priv->hal, 1);

		ret = rpu_prog_reset(reset_type, LMAC_MODE_NORMAL);
		if (ret != 0)
			goto prog_rpu_fail;

	if (wait_for_reset_complete(priv, 1) < 0) {
		ret = -1;
		rk915_err("%s: wait_for_reset_complete failed\n", __func__);
		goto hal_deinit_bufs;
	}

	ret = rpu_fw_priv_cmd(FW_PRIV_INIT, NULL);
	if (ret != 0)
		goto prog_rpu_fail;

	if (rk915_patch_features) {
		ret = rpu_prog_patch_feature(rk915_patch_features);
		if (ret != 0)
			goto prog_rpu_fail;
	}

	rpu_lmac_feature_init();

	//prog_sleep_controller_default();

	ret = rpu_prog_txpower(priv->txpower);
	if (ret != 0)
		goto prog_rpu_fail;

	rpu_tx_init(priv);

#ifdef ENABLE_DAPT
	dapt_param_init(priv);
#endif

	return 0;
hal_deinit_bufs:
	hal_ops.deinit_bufs(priv->hal);
prog_rpu_fail:
hal_stop:
	hal_ops.stop(priv->hal);
rpu_if_deinit:
	rpu_if_deinit();
	return ret;
}


void rpu_core_deinit(struct img_priv *priv)
{
	int ret = 0;

	rk915_dbg(RK915_DBG_MAIN, "%s-UMAC: De-init called\n", priv->name);

#ifdef ENABLE_DAPT
	dapt_param_deinit(priv);
#endif

	/* De initialize tx  and disable LMAC*/
	rpu_tx_deinit(priv);

	if (!priv->hal->fw_error) {
		/* Disable the LMAC */
		priv->reset_complete = 0;
		rk915_dbg(RK915_DBG_MAIN, "%s-UMAC: Reset (DISABLE)\n", priv->name);

		/* Make sure the chip is awake before it is asked to stop:
		 * the enable path pokes it the same way.
		 */
		rk915_notify_pm(priv->hal, 1);

			ret = rpu_prog_reset(LMAC_DISABLE, LMAC_MODE_NORMAL);
			if (ret != 0)
				goto prog_rpu_fail;

		if (wait_for_reset_complete(priv, 0) < 0) {
			ret = -1;
			rk915_err("%s: wait_for_reset_complete failed\n", __func__);
			goto prog_rpu_fail;
		}

		/* The firmware halts once disabled and no longer serves the
		 * bus; quiesce all traffic until the next bring-up (which
		 * clears this again), and let the bus clock gate while
		 * wifi is off.
		 */
		block_rpu_comm = true;
		rk915_sdio_clock_release(priv->hal->io_info);

		/* notify fw sleep */
		rk915_notify_pm(priv->hal, 0);
	}

prog_rpu_fail:
	wait_for_fw_error_process_complete(priv);

	rpu_if_free_outstnding(priv->hal);

	hal_ops.stop(priv->hal);
	hal_ops.deinit_bufs(priv->hal);

	rpu_if_deinit();

	priv->state = STOPPED;
}


void rpu_reset_complete(char *lmac_version, void *context)
{
	struct img_priv *priv = (struct img_priv *)context;

	memcpy(priv->stats->rpu_lmac_version, lmac_version, 5);
	priv->stats->rpu_lmac_version[5] = '\0';
	priv->reset_complete = 1;
	rk915_wake_waiters(priv->hal);
	if (print_version) {
		print_version = 0;
		memcpy(priv->stats->fw_version, lmac_version+6, 20);
		priv->stats->fw_version[20] = '\0';
		rk915_info("firmware patch %s, build %s\n",
						priv->stats->rpu_lmac_version, priv->stats->fw_version);
	}
}

void rpu_fw_info_dump_start(void *context, unsigned int type, unsigned int reg)
{
	struct img_priv *priv = (struct img_priv *)context;

	priv->fw_info->finish = 0;
	priv->fw_info->offset = 0;
	priv->fw_info->type = type;
	if (type == DUMP_REG_INFO)
		priv->fw_info->reg = reg;
}

void rpu_fw_priv_cmd_done(struct fw_priv_cmd_done *event,
			   void *context)
{
	struct img_priv *priv = (struct img_priv *)context;

	if (priv->fw_info->offset+event->info.size >= priv->fw_info->len) {
		rk915_err("%s: fw_info buf overflow\n", __func__);
		return;
	}

	memcpy(priv->fw_info->info + priv->fw_info->offset,
					event->info.data, event->info.size);
	priv->fw_info->type = event->hdr.descriptor_id;
	priv->fw_info->offset += event->info.size;
	if (event->info.end) {
		priv->fw_info->info[priv->fw_info->offset] = 0;
		priv->fw_info->finish = 1;
	}

	if (event->hdr.descriptor_id == DUMP_FW_CRASH_INFO)
		rk915_err("\n%s\n", priv->fw_info->info);
}

void rpu_mac_stats(struct umac_event_mac_stats *mac_stats,
			   void *context)
{
	struct img_priv *priv = (struct img_priv *)context;

	/* TX related */
	priv->stats->roc_start = mac_stats->roc_start;
	priv->stats->roc_stop = mac_stats->roc_stop;
	priv->stats->roc_complete = mac_stats->roc_complete;
	priv->stats->roc_stop_complete = mac_stats->roc_stop_complete;
	priv->stats->tx_cmd_cnt = mac_stats->tx_cmd_cnt;
	priv->stats->tx_done_cnt = mac_stats->tx_done_cnt;
	priv->stats->tx_edca_trigger_cnt = mac_stats->tx_edca_trigger_cnt;
	priv->stats->tx_edca_isr_cnt = mac_stats->tx_edca_isr_cnt;
	priv->stats->tx_start_cnt = mac_stats->tx_start_cnt;
	priv->stats->tx_abort_cnt = mac_stats->tx_abort_cnt;
	priv->stats->tx_abort_isr_cnt = mac_stats->tx_abort_isr_cnt;
	priv->stats->tx_underrun_cnt = mac_stats->tx_underrun_cnt;
	priv->stats->tx_rts_cnt = mac_stats->tx_rts_cnt;
	priv->stats->tx_ampdu_cnt = mac_stats->tx_ampdu_cnt;
	priv->stats->tx_mpdu_cnt = mac_stats->tx_mpdu_cnt;
	priv->stats->tx_crypto_post = mac_stats->tx_crypto_post;
	priv->stats->tx_crypto_done = mac_stats->tx_crypto_done;
	priv->stats->rx_pkt_to_umac = mac_stats->rx_pkt_to_umac;
	priv->stats->rx_crypto_post = mac_stats->rx_crypto_post;
	priv->stats->rx_crypto_done = mac_stats->rx_crypto_done;
	/* RX related */
	priv->stats->rx_isr_cnt = mac_stats->rx_isr_cnt;
	priv->stats->rx_ack_cts_to_cnt = mac_stats->rx_ack_cts_to_cnt;
	priv->stats->rx_cts_cnt = mac_stats->rx_cts_cnt;
	priv->stats->rx_ack_resp_cnt = mac_stats->rx_ack_resp_cnt;
	priv->stats->rx_ba_resp_cnt = mac_stats->rx_ba_resp_cnt;
	priv->stats->rx_fail_in_ba_bitmap_cnt =
		mac_stats->rx_fail_in_ba_bitmap_cnt;
	priv->stats->rx_circular_buffer_free_cnt =
		mac_stats->rx_circular_buffer_free_cnt;
	priv->stats->rx_mic_fail_cnt = mac_stats->rx_mic_fail_cnt;

	/* HAL related */
	priv->stats->hal_cmd_cnt = mac_stats->hal_cmd_cnt;
	priv->stats->hal_event_cnt = mac_stats->hal_event_cnt;
	priv->stats->hal_ext_ptr_null_cnt = mac_stats->hal_ext_ptr_null_cnt;

	/* LPW PHY Related */
	priv->stats->csync_timeout_cntr = mac_stats->csync_timeout_cntr;
	priv->stats->fsync_timeout_cntr = mac_stats->fsync_timeout_cntr;
	priv->stats->acdrop_timeout_cntr = mac_stats->acdrop_timeout_cntr;
	priv->stats->csync_abort_agctrig_cntr = mac_stats->csync_abort_agctrig_cntr;
	priv->stats->crc_success_cnt = mac_stats->crc_success_cnt;
	priv->stats->crc_fail_cnt = mac_stats->crc_fail_cnt;
	priv->stats->rpu_boot_cnt = mac_stats->rpu_boot_cnt;
	memcpy(priv->stats->sleep_stats, mac_stats->sleep_stats,
		sizeof(priv->stats->sleep_stats));
}


void rpu_ch_prog_complete(int event,
				  struct umac_event_ch_prog_complete *prog_ch,
				  void *context)
{
	struct img_priv *priv = (struct img_priv *)context;

	priv->chan_prog_done = 1;
	rk915_wake_waiters(priv->hal);
}

int rk915_wait_fw_ready_to_sleep(struct hal_priv *hal)
{
	struct img_priv *imgpriv =
		(hal->wifi && hal->wifi->hw) ? hal->wifi->hw->priv : NULL;

	/* a disabled firmware no longer answers: treat as ready */
	if (block_rpu_comm)
		return 1;

	if (imgpriv) {
		imgpriv->read_csr_complete = 0;
		imgpriv->read_csr_value = 0;
		rpu_prog_read_csr(0xbf2);
		wait_for_read_csr_cmp(imgpriv);

		if (imgpriv->read_csr_value & (1 << 14)) {
			imgpriv->read_csr_complete = 0;
			imgpriv->read_csr_value = 0;
			rpu_prog_read_csr(0xbf2);
			wait_for_read_csr_cmp(imgpriv);
		}

		return !!(imgpriv->read_csr_value & (1 << 15));
	}

	return 0;
}

