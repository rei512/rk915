// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/debugfs.h>
#include <linux/moduleparam.h>

#include "core.h"
#include "utils.h"
#include "version.h"
#include "hal_io.h"
#include "if_io.h"

/*
 * Must stay 1 while the driver has no wake path: letting the LMAC sleep
 * leaves CMD52 reads returning 0xF0F0 once the link goes idle.
 */
unsigned int lpw_no_sleep = 1;
module_param(lpw_no_sleep, uint, 0444);
MODULE_PARM_DESC(lpw_no_sleep,
		 "keep the LMAC permanently awake (default 1; 0 needs a wake path)");

unsigned int default_phy_threshold = DAPT_DEFAULT_PHY_THRESH;


#undef IEEE80211_BAND_2GHZ
#define IEEE80211_BAND_2GHZ NL80211_BAND_2GHZ

#include "sdio.h"
static int proc_read_sleep_stats(struct seq_file *m, void *v)
{
	struct wifi_dev *wifi = m->private;
	int i;

	for (i = 0; i < 12; i++)
		seq_printf(m, "stats[%d] = %d\n", i,
			wifi->stats.sleep_stats[i]);
	seq_printf(m, "rpu_boot_cnt=%d\n",
		   wifi->stats.rpu_boot_cnt);
	seq_printf(m, "fw state: %d\n", rk915_readb(wifi->hal, IO_FW_STATE));

	return 0;
}

static int proc_open_sleep_stats(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_sleep_stats, inode->i_private);
}

static const struct file_operations params_fops_sleep_stats = {
	.open = proc_open_sleep_stats,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = NULL,
	.release = single_release
};

static int proc_read_config(struct seq_file *m, void *v)
{
	struct wifi_dev *wifi = m->private;
	struct img_priv *priv;

	if (!wifi->hw)
		return -ENODEV;

	priv = (struct img_priv *)(wifi->hw->priv);

	seq_printf(m, "debug_mask = 0x%x\n", rk915_debug_mask);
	seq_printf(m, "fw_loaded = %d\n", wifi->params.fw_loaded);
	seq_printf(m, "fw_error_counter = %d\n", wifi->hal->fw_error_counter);
	seq_printf(m, "state = %d\n", priv->state);
	seq_printf(m, "power_save = %d\n", priv->power_save);

	return 0;
}


static int proc_read_phy_stats(struct seq_file *m, void *v)
{
	struct wifi_dev *wifi = m->private;

	seq_puts(m, "************* BB Stats ***********\n");

	seq_printf(m, "csync_timeout_cntr  =%x\n",
		   wifi->stats.csync_timeout_cntr);
	seq_printf(m, "fsync_timeout_cntr  =%x\n",
		   wifi->stats.fsync_timeout_cntr);
	seq_printf(m, "acdrop_timeout_cntr  =%x\n",
		   wifi->stats.acdrop_timeout_cntr);
	seq_printf(m, "csync_abort_agctrig_cntr  =%x\n",
		   wifi->stats.csync_abort_agctrig_cntr);
	seq_printf(m, "crc_success_cnt  =%d\n",
		   wifi->stats.crc_success_cnt);
	seq_printf(m, "crc_fail_cnt  =%d\n",
		   wifi->stats.crc_fail_cnt);

	return 0;
}

static void dump_tx_buff_info(struct seq_file *m, struct tx_config *tx)
{
	int i, j;
	struct sk_buff_head *pend_pkt_q;

	seq_puts(m, "tx_buff_pool_map (LE) =\n\t");
	for (i = 0; i < NUM_TX_DESCS; i++) {
		if (test_bit(i, &tx->buf_pool_bmp[0]))
			seq_puts(m, "1 ");
		else
			seq_puts(m, "0 ");
		if (((i+1)%5) == 0)
			seq_puts(m, ", ");
	}
	seq_puts(m, "\n");

	seq_puts(m, "outstanding_pkts =\n\t");
	for (i = 0; i < NUM_TX_DESCS; i++) {
		seq_printf(m, "%d ", tx->outstanding_pkts[i]);
		if (((i+1)%5) == 0)
			seq_puts(m, ", ");
	}
	seq_puts(m, "\n");

	seq_puts(m, "outstanding_tokens =\n\t");
	for (i = 0; i < NUM_ACS; i++)
		seq_printf(m, "%d ", tx->outstanding_tokens[i]);
	seq_puts(m, "\n");

	seq_puts(m, "curr_peer_opp =\n\t");
	for (i = 0; i < NUM_ACS; i++)
		seq_printf(m, "%d ", tx->curr_peer_opp[i]);
	seq_puts(m, "\n");

	seq_puts(m, "queue_stopped_bmp =\n\t");
	for (i = 0; i < NUM_ACS; i++) {
		if (tx->queue_stopped_bmp & (1 << i))
			seq_puts(m, "1 ");
		else
			seq_puts(m, "0 ");
	}
	seq_puts(m, "\n");

	seq_puts(m, "pending_pkt =\n");
	for (j = 0; j < MAX_PEND_Q_PER_AC; j++) {
		seq_puts(m, "\t");
		for (i = 0; i < NUM_ACS; i++) {
			pend_pkt_q = &tx->pending_pkt[j][i];
			seq_printf(m, "%03d ", skb_queue_len(pend_pkt_q));
		}
		seq_puts(m, "\n");
	}
}

static int proc_read_mac_stats(struct seq_file *m, void *v)
{
	struct wifi_dev *wifi = m->private;
	unsigned int index;
	struct img_priv *priv = NULL;

	if (!wifi->hw)
		return -ENODEV;

	priv = (struct img_priv *)(wifi->hw->priv);



	seq_puts(m, "************* UMAC STATS ***********\n");
	seq_printf(m, "rx_packet_mgmt_count = %d\n",
		   wifi->stats.rx_packet_mgmt_count);
	seq_printf(m, "rx_packet_data_count = %d\n",
		   wifi->stats.rx_packet_data_count);
	seq_printf(m, "tx_packet_count(HT MCS0) = %d\n",
		   wifi->stats.ht_tx_mcs0_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS1) = %d\n",
		   wifi->stats.ht_tx_mcs1_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS2) = %d\n",
		   wifi->stats.ht_tx_mcs2_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS3) = %d\n",
		   wifi->stats.ht_tx_mcs3_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS4) = %d\n",
		   wifi->stats.ht_tx_mcs4_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS5) = %d\n",
		   wifi->stats.ht_tx_mcs5_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS6) = %d\n",
		   wifi->stats.ht_tx_mcs6_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS7) = %d\n",
		   wifi->stats.ht_tx_mcs7_packet_count);

	if (wifi->params.uccp_num_spatial_streams == 2) {
		seq_printf(m, "tx_packet_count(HT MCS8) = %d\n",
			   wifi->stats.ht_tx_mcs8_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS9) = %d\n",
			   wifi->stats.ht_tx_mcs9_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS10) = %d\n",
			   wifi->stats.ht_tx_mcs10_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS11) = %d\n",
			   wifi->stats.ht_tx_mcs11_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS12) = %d\n",
			   wifi->stats.ht_tx_mcs12_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS13) = %d\n",
			   wifi->stats.ht_tx_mcs13_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS14) = %d\n",
			   wifi->stats.ht_tx_mcs14_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS15) = %d\n",
			   wifi->stats.ht_tx_mcs15_packet_count);
	}
	seq_printf(m, "tx_cmds_from_stack= %d\n",
		   wifi->stats.tx_cmds_from_stack);
	seq_printf(m, "tx_dones_to_stack= %d\n",
		   wifi->stats.tx_dones_to_stack);
	seq_printf(m, "tx_noagg_not_addr= %d\n",
		   wifi->stats.tx_noagg_not_addr);
	seq_printf(m, "tx_noagg_not_ampdu= %d\n",
		   wifi->stats.tx_noagg_not_ampdu);
	seq_printf(m, "tx_noagg_not_qos= %d\n",
		   wifi->stats.tx_noagg_not_qos);
	seq_printf(m, "outstanding_cmd_cnt = %d (%d %d)\n",
		   wifi->stats.outstanding_cmd_cnt, skb_queue_len(&cmd_info.outstanding_cmd),
		   priv->stats->max_outstanding_cmd_queue_cnt);
	seq_printf(m, "gen_cmd_send_count = %d\n",
		   wifi->stats.gen_cmd_send_count);
	seq_printf(m, "umac_scan_req = %d\n",
		   wifi->stats.umac_scan_req);
	seq_printf(m, "umac_scan_complete = %d\n",
		   wifi->stats.umac_scan_complete);
	seq_printf(m, "hw_scan_status = %d\n",
		   wifi->params.hw_scan_status);
	seq_printf(m, "roc_in_progress = %d\n",
			priv->roc_params.roc_in_progress);
	seq_printf(m, "roc_starting = %d\n",
			priv->roc_params.roc_starting);
	seq_printf(m, "tx_cmd_send_count_single = %d\n",
		   wifi->stats.tx_cmd_send_count_single);
	seq_printf(m, "tx_cmd_send_count_multi = %d\n",
		   wifi->stats.tx_cmd_send_count_multi);
	seq_printf(m, "tx_cmd_send_count_beacon_q = %d\n",
		   wifi->stats.tx_cmd_send_count_beaconq);
	seq_printf(m, "tx_done_recv_count = %d\n",
		   wifi->stats.tx_done_recv_count);

	seq_printf(m, "tx_buff_pool_map = %x\n",
		   (unsigned int)priv->tx.buf_pool_bmp[0]);
	dump_tx_buff_info(m, &priv->tx);

	seq_puts(m, "************* LMAC STATS ***********\n");
	seq_printf(m, "roc_start =%d\n",
		   wifi->stats.roc_start);
	seq_printf(m, "roc_stop =%d\n",
		   wifi->stats.roc_stop);
	seq_printf(m, "roc_complete =%d\n",
		   wifi->stats.roc_complete);
	seq_printf(m, "roc_stop_complete =%d\n",
		   wifi->stats.roc_stop_complete);
	/* TX related */
	seq_printf(m, "tx_cmd_cnt =%d\n",
		   wifi->stats.tx_cmd_cnt);
	seq_printf(m, "tx_done_cnt =%d\n",
		   wifi->stats.tx_done_cnt);
	seq_printf(m, "tx_edca_trigger_cnt =%d\n",
		   wifi->stats.tx_edca_trigger_cnt);
	seq_printf(m, "tx_edca_isr_cnt =%d\n",
		   wifi->stats.tx_edca_isr_cnt);
	seq_printf(m, "tx_start_cnt =%d\n",
		   wifi->stats.tx_start_cnt);
	seq_printf(m, "tx_abort_cnt =%d\n",
		   wifi->stats.tx_abort_cnt);
	seq_printf(m, "tx_abort_isr_cnt =%d\n",
		   wifi->stats.tx_abort_isr_cnt);
	seq_printf(m, "tx_underrun_cnt =%d\n",
		   wifi->stats.tx_underrun_cnt);
	seq_printf(m, "tx_rts_cnt =%d\n",
		   wifi->stats.tx_rts_cnt);
	seq_printf(m, "tx_ampdu_cnt =%d\n",
		   wifi->stats.tx_ampdu_cnt);
	seq_printf(m, "tx_mpdu_cnt =%d\n",
		   wifi->stats.tx_mpdu_cnt);
	seq_printf(m, "tx_crypto_post =%d\n",
		   wifi->stats.tx_crypto_post);
	seq_printf(m, "tx_crypto_done =%d\n",
		   wifi->stats.tx_crypto_done);
	seq_printf(m, "rx_pkt_to_umac =%d\n",
		   wifi->stats.rx_pkt_to_umac);
	seq_printf(m, "rx_crypto_post =%d\n",
		   wifi->stats.rx_crypto_post);
	seq_printf(m, "rx_crypto_done =%d\n",
		   wifi->stats.rx_crypto_done);
	/* RX related */
	seq_printf(m, "rx_isr_cnt  =%d\n",
		   wifi->stats.rx_isr_cnt);
	seq_printf(m, "rx_ack_cts_to_cnt =%d\n",
		   wifi->stats.rx_ack_cts_to_cnt);
	seq_printf(m, "rx_cts_cnt =%d\n",
		   wifi->stats.rx_cts_cnt);
	seq_printf(m, "rx_ack_resp_cnt =%d\n",
		   wifi->stats.rx_ack_resp_cnt);
	seq_printf(m, "rx_ba_resp_cnt =%d\n",
		   wifi->stats.rx_ba_resp_cnt);
	seq_printf(m, "rx_fail_in_ba_bitmap_cnt =%d\n",
		   wifi->stats.rx_fail_in_ba_bitmap_cnt);
	seq_printf(m, "rx_circular_buffer_free_cnt =%d\n",
		   wifi->stats.rx_circular_buffer_free_cnt);
	seq_printf(m, "rx_mic_fail_cnt =%d\n",
		   wifi->stats.rx_mic_fail_cnt);

	/* HAL related */
	seq_printf(m, "hal_cmd_cnt  =%d\n",
		   wifi->stats.hal_cmd_cnt);
	seq_printf(m, "hal_event_cnt =%d\n",
		   wifi->stats.hal_event_cnt);
	seq_printf(m, "hal_ext_ptr_null_cnt =%d\n",
		   wifi->stats.hal_ext_ptr_null_cnt);
	seq_printf(m, "fw_error_counter = %d\n",
			wifi->hal->fw_error_counter);
	seq_printf(m, "fw_error_counter_scan = %d\n",
			wifi->hal->fw_error_counter_scan);
	seq_printf(m, "lpw_error_counter = %d\n",
			wifi->hal->lpw_error_counter);

	/* power save */
	seq_printf(m, "wifi power save (%s)\n",
			priv->power_save ? "AWAKE":"SLEEP");

	/* interface info */
	seq_printf(m, "current_vif_count = %d\n", priv->current_vif_count);
	seq_printf(m, "active_vifs = %d\n", priv->active_vifs);
	for (index = 0; index < MAX_VIFS; index++) {
		struct ieee80211_vif *vif = priv->vifs[index];
		struct umac_vif *uvif;

		if (!vif)
			break;
		uvif = (struct umac_vif *)&vif->drv_priv;
		if (!uvif)
			break;
		seq_printf(m, "\tvif_index %d\n", uvif->vif_index);
		seq_printf(m, "\ttype = %d\n", vif->type);
		seq_printf(m, "\taddr %pM\n", vif->addr);
		seq_printf(m, "\tbssid %pM\n", uvif->bssid);
	}

#ifdef ENABLE_DAPT
	/*dapt info */
	seq_puts(m, "dapt info:\n");
	seq_printf(m, "main_index = %d\n", priv->dapt_params.main_index);
	seq_printf(m, "p2p_index = %d\n", priv->dapt_params.p2p_index);
	seq_printf(m, "conn_state[0] = %d, conn_state[1] = %d\n",
				priv->dapt_params.conn_state[0], priv->dapt_params.conn_state[1]);
	seq_printf(m, "iftype = %d\n", priv->iftype);
	seq_printf(m, "dapt_thresh_offset = %d\n", priv->params->dapt_thresh_offset);
	seq_printf(m, "dapt_thresh_exponent = %d\n", priv->params->dapt_thresh_exponent);
	seq_printf(m, "dapt_thresh_min = %d\n", priv->params->dapt_thresh_min);
	seq_printf(m, "dapt_thresh_max = %d\n", priv->params->dapt_thresh_max);
	for (index = 0; index < MAX_VIFS; index++) {
		seq_printf(m, "\tvif_addr = %pM, bssid = %pM, conn_state = %d\n",
					priv->dapt_params.vif_addr[index],
					priv->dapt_params.bssid[index],
					priv->dapt_params.conn_state[index]);
		seq_printf(m, "thresh_accum = %d\n", priv->dapt_params.thresh_accum[index]);
		seq_printf(m, "avg_thresh = %d\n", priv->dapt_params.avg_thresh[index]);
		seq_printf(m, "new_thresh = %d\n", priv->dapt_params.new_thresh[index]);
	}
	seq_puts(m, "cur_seted_thresh:\n\t");
	for (index = 0; index < 14; index++)
		seq_printf(m, "%03d ", priv->dapt_params.cur_seted_thresh[index]);

	seq_puts(m, "\nthreld history:\n");
	for (index = 0; index < 14; index++) {
		int s;

		if (ieee80211_frequency_to_channel(priv->cur_chan.center_freq1) == index + 1)
			seq_printf(m, "\t ***channel %02d: offset %02d: ", index + 1, priv->dapt_params.cur_thr_offset[index]);
		else
			seq_printf(m, "\t channel %02d: offset %02d: ", index + 1, priv->dapt_params.cur_thr_offset[index]);
		for (s = 0; s < DAPT_SETED_PHY_THRESH_COUNT; s++) {
			if (priv->dapt_params.cur_thr_offset[index] == s + 1) {
				seq_puts(m, "***");
			} else if (priv->dapt_params.cur_thr_offset[index] == 0) {
				if (s == DAPT_SETED_PHY_THRESH_COUNT - 1)
					seq_puts(m, "***");
			}
			seq_printf(m, "%03d ", priv->dapt_params.thr_history[index][s]);
		}
		seq_puts(m, "\n");
	}
#endif

	seq_printf(m, "rxq len = %d\n", skb_queue_len(&wifi->hal->rxq));
	seq_printf(m, "max_rxq len = %d\n", wifi->hal->max_rxq_len);
	seq_printf(m, "txq len = %d\n", skb_queue_len(&wifi->hal->txq));

	seq_printf(m, "cmd_reset_count = %d\n", priv->cmd_reset_count);

	seq_printf(m, "null_frame_send_count = %d\n", priv->null_frame_send_count);

	seq_printf(m, "tx_retry_frm_cnt: %d\n", priv->tx_retry_frm_cnt);
	return 0;

}

struct time_info {
	unsigned int count;
	unsigned int max_time;
	unsigned long long total_time;
};

struct vif_info {
	int if_ctrl;
	int if_idx;
	int if_mode;
	int if_conn_sta;
	unsigned char if_addr[6];
	unsigned char bssid[6];
	int key_ctrl;
	int key_type;
};

#define MAX_IF 2
struct if_info {
	int num;
	struct vif_info vif_info[MAX_IF];
};

struct filter_pkt_info {
	unsigned int total_pkt;
	unsigned int probe_req_pkt;
	unsigned int bcast_pkt;
	unsigned int mcast_pkt;
};

struct tx_rx_count_info {
	unsigned int cmd_tx_send;
	unsigned int cmd_send;
	unsigned int event_recv;
	unsigned int event_rx_recv;
	unsigned int event_rx_pkt_recv;
	unsigned int event_rx_pkt_crc_ok;
	unsigned int event_rx_pkt_crc_err;
	unsigned int event_tx_done_recv;
	unsigned int event_rx_serias;
	unsigned short err_desc_id_lmac;
	unsigned short err_desc_id_host;
	unsigned short lpw_hang;
	unsigned short lpw_hang_cnt;
	unsigned short cmd_cnt_dur_lpw_hang;
	unsigned short cmd_txcnt_dur_lpw_hang;
	unsigned short cmd_rxcnt_dur_lpw_hang;
	struct time_info wifi_isr_info;
	struct time_info sdio_isr_info[4];
	struct time_info cmd_send_info;
	unsigned int cmd_id;
	unsigned long long total_tick;

	struct time_info rx_notify;
	struct time_info rx_begin;
	struct time_info rx_end;
	struct time_info rx_interval;
	int lpw_rx_q_min;
	short wifi_int_disabled;

	struct time_info tx_done;
	struct time_info scan_hang;

	struct filter_pkt_info filter_info;
};

static void dump_time_info(struct seq_file *m,
					struct time_info *info, char *str)
{
	unsigned long long value;

	/* total_time from fw unit is 25ns, so need to div 40 (convert to us) */
	value = info->total_time>>2;
	if (info->count != 0)
		do_div(value, info->count);
	seq_printf(m, "%s:\n"
				"\ttotal_time = %lld\b us\n"
				"\tmax_time = %d us\n"
				"\tcount = %d\n"
				"\tavg = %d us\n",
				str,
				info->total_time>>2,
				info->max_time/40,
				info->count,
				(unsigned int)value/10);
}

static void dump_if_info(struct seq_file *m,
					struct if_info *info)
{
	int i;

	for (i = 0; i < MAX_IF; i++) {
		seq_printf(m, "if_idx %d:\n", info->vif_info[i].if_idx);
		seq_printf(m, "\tif_ctrl=%s\n", info->vif_info[i].if_ctrl == IF_ADD ? "ADD":"DEL");
		switch (info->vif_info[i].if_mode) {
		case IF_MODE_STA_BSS:
			seq_puts(m, "\tif_mode=IF_MODE_STA_BSS\n");
			break;
		case IF_MODE_STA_IBSS:
			seq_puts(m, "\tif_mode=IF_MODE_STA_IBSS\n");
			break;
		case IF_MODE_AP:
			seq_puts(m, "\tif_mode=IF_MODE_AP\n");
			break;
		default:
			seq_puts(m, "\tif_mode=UNKNOW\n");
			break;
		}
		seq_printf(m, "\tif_conn_sta=%s\n", info->vif_info[i].if_conn_sta == STA_CONN ? "STA_CONN":"STA_DISCONN");
		seq_printf(m, "\tif_addr=%pM\n", info->vif_info[i].if_addr);
		seq_printf(m, "\tbssid=%pM\n", info->vif_info[i].bssid);
		seq_printf(m, "\tkey_ctrl=%s\n", info->vif_info[i].key_ctrl == KEY_CTRL_ADD ? "ADD":"DEL");
		seq_printf(m, "\tkey_type=%d\n", info->vif_info[i].key_type);
	}
}

static void *fw_log_seq_start(struct seq_file *s, loff_t *pos)
{
	static int read_finish;
	struct wifi_dev *wifi = s->private;
	struct img_priv *priv;
	struct fw_info_dump *fw_info = &wifi->fw_info;

	pr_debug("%s: %d\n", __func__, read_finish);

	if (read_finish == 1) {
		read_finish = 0;
		return NULL;// no more data to read, exit
	}

	if (!wifi->hw)
		return NULL;

	priv = (struct img_priv *)(wifi->hw->priv);
	if (priv->state != STARTED) {
		pr_err("Interface is not initialized\n");
		return NULL;
	}

	if (rpu_fw_priv_cmd_sync(DUMP_FW_LOG, NULL) != 0) {
		pr_err("%s: send cmd failed\n", __func__);
		return NULL;
	}
	pr_debug("%s: read log %d\n", __func__, fw_info->offset);

	fw_info->finish = 0;

#define PRIV_CMD_DONE_EVENT_HEADER_SIZE (sizeof(struct host_rpu_msg_hdr) + sizeof(struct dump_info) - 1)

	read_finish = (fw_info->offset < (128-PRIV_CMD_DONE_EVENT_HEADER_SIZE))?1:0;

	return fw_info->info;
}

static void *fw_log_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	return NULL;
}

static void fw_log_seq_stop(struct seq_file *s, void *v)
{
}

static int fw_log_seq_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%s", (char *)v);
	return 0;
}

static const struct seq_operations fw_log_seq_ops = {
	.start = fw_log_seq_start,
	.next  = fw_log_seq_next,
	.stop  = fw_log_seq_stop,
	.show  = fw_log_seq_show
};

static void dump_txrx_count_info(struct seq_file *m, struct fw_info_dump *fw_info)
{
	unsigned long long result = 0, delta1, delta2;
	struct tx_rx_count_info *info = (struct tx_rx_count_info *)fw_info->info;

	seq_printf(m, "cmd_tx_send = %d\n"
				 "cmd_send = %d\n"
				 "event_recv = %d\n"
				 "event_rx_recv = %d\n"
				 "event_rx_pkt_recv = %d\n"
				 "event_rx_pkt_crc_ok = %d\n"
				 "event_rx_pkt_crc_err = %d\n"
				 "event_tx_done_recv = %d\n"
				 "event_rx_serias = %d\n"
				 "err_desc_id_lmac = %d\n"
				 "err_desc_id_host = %d\n"
				 "lpw_hang = %d\n"
				 "lpw_hang_cnt = %d\n"
				 "cmd_cnt_dur_lpw_hang = %d\n"
				 "cmd_txcnt_dur_lpw_hang = %d\n"
				 "cmd_rxcnt_dur_lpw_hang = %d\n",
				 info->cmd_tx_send,
				 info->cmd_send,
				 info->event_recv,
				 info->event_rx_recv,
				 info->event_rx_pkt_recv,
				 info->event_rx_pkt_crc_ok,
				 info->event_rx_pkt_crc_err,
				 info->event_tx_done_recv,
				 info->event_rx_serias,
				 info->err_desc_id_lmac,
				 info->err_desc_id_host,
				 info->lpw_hang,
				 info->lpw_hang_cnt,
				 info->cmd_cnt_dur_lpw_hang,
				 info->cmd_txcnt_dur_lpw_hang,
				 info->cmd_rxcnt_dur_lpw_hang);

	dump_time_info(m, &info->wifi_isr_info, "wifi isr info");
	result += info->wifi_isr_info.total_time;

	dump_time_info(m, &info->sdio_isr_info[0], "sdio isr (COMP)");
	result += info->sdio_isr_info[0].total_time;
	dump_time_info(m, &info->sdio_isr_info[1], "sdio isr (DMA)");
	result += info->sdio_isr_info[1].total_time;
	dump_time_info(m, &info->sdio_isr_info[2], "sdio isr (WR)");
	result += info->sdio_isr_info[2].total_time;
	dump_time_info(m, &info->sdio_isr_info[3], "sdio isr (RD)");
	result += info->sdio_isr_info[3].total_time;

	delta1 = result - fw_info->last_total_isr_tick;
	delta2 = info->total_tick - fw_info->last_total_tick;
	do_div(delta1, 40*1000);
	do_div(delta2, 40*1000);
	seq_printf(m, "isr/total tick (%lld/%lld ms)\n",
			delta1, delta2);
	fw_info->last_total_isr_tick = result;
	fw_info->last_total_tick = info->total_tick;

	dump_time_info(m, &info->cmd_send_info, "cmd send info");
	seq_printf(m, "cmd id: %d\n", info->cmd_id);

	dump_time_info(m, &info->rx_notify, "rx_notify");
	dump_time_info(m, &info->rx_begin, "rx_begin");
	dump_time_info(m, &info->rx_end, "rx_end");
	dump_time_info(m, &info->rx_interval, "rx_interval");

	seq_printf(m, "min lpw q: %d\n", info->lpw_rx_q_min);

	dump_time_info(m, &info->tx_done, "tx_done");

	dump_time_info(m, &info->scan_hang, "scan_hang");

	seq_printf(m, "wifi_int_disabled: %d\n", info->wifi_int_disabled);

	seq_puts(m, "dump filter info:\n");
	seq_printf(m, "\ttotal_pkt: %d\n", info->filter_info.total_pkt);
	seq_printf(m, "\tprobe_req_pkt: %d\n", info->filter_info.probe_req_pkt);
	seq_printf(m, "\tbcast_pkt: %d\n", info->filter_info.bcast_pkt);
	seq_printf(m, "\tmcast_pkt: %d\n", info->filter_info.mcast_pkt);
}

static void *fw_info_seq_start(struct seq_file *s, loff_t *pos)
{
	static int read_finish;
	struct wifi_dev *wifi = s->private;
	struct img_priv *priv;
	struct fw_info_dump *fw_info = &wifi->fw_info;

	pr_debug("%s: %d\n", __func__, read_finish);

	if (read_finish == 1) {
		read_finish = 0;
		return NULL;// no more data to read, exit
	}

	if (!wifi->hw)
		return NULL;

	priv = (struct img_priv *)(wifi->hw->priv);
	if (priv->state != STARTED) {
		pr_err("Interface is not initialized\n");
		return NULL;
	}

	if (fw_info->type == ADC_CAPTURE || fw_info->type == DUMP_ADC_CAPTURE_DATA) {
		if (rpu_fw_priv_cmd_sync(DUMP_ADC_CAPTURE_DATA, NULL) != 0) {
			pr_err("%s: send cmd failed\n", __func__);
			return NULL;
		}
#define PRIV_CMD_DONE_EVENT_HEADER_SIZE (sizeof(struct host_rpu_msg_hdr) + sizeof(struct dump_info) - 1)
		read_finish = (fw_info->offset < (128-PRIV_CMD_DONE_EVENT_HEADER_SIZE))?1:0;
	} else {
		if (!fw_info->finish)
			return NULL;
		read_finish = 1;
	}
	pr_debug("%s: read %d\n", __func__, fw_info->offset);

	fw_info->finish = 0;


	return (void *)fw_info;
}

static void dump_bytes(struct seq_file *s, struct fw_info_dump *info, int word)
{
	int i, j, line;
	u8 *buf_byte = (u8 *)info->info;
	u32 *buf_word = (u32 *)info->info;

	if (word)
		line = 4;
	else
		line = 16;

	for (i = 0; i < round_up(info->offset, 16)/16; i++) {
		for (j = 0; j < line; j++) {
			if (word)
				seq_printf(s, "%08x ", *buf_word++);
			else
				seq_printf(s, "%02x ", *buf_byte++);
		}
		seq_puts(s, "\n");
	}
	seq_puts(s, "\n");
}

static int fw_info_seq_show(struct seq_file *s, void *v)
{
	struct fw_info_dump *fw_info = (struct fw_info_dump *)v;

	if (fw_info->type == DUMP_REG_INFO) {
		int i;
		unsigned int *buf = (unsigned int *)fw_info->info;

		for (i = 0; i < fw_info->offset/4; i++) {
			if ((i%4) == 0)
				seq_printf(s, "\n%08x: ", fw_info->reg);
			seq_printf(s, "%08x ", *buf++);
			fw_info->reg += 4;
		}
		seq_puts(s, "\n");
	} else if (fw_info->type == DUMP_TXRX_COUNT_INFO) {
		dump_txrx_count_info(s, fw_info);
	} else if (fw_info->type == DUMP_IF_INFO) {
		dump_if_info(s, (struct if_info *)fw_info->info);
	} else if (fw_info->type == DUMP_ADC_CAPTURE_DATA) {
		unsigned int len = fw_info->offset;
		unsigned int *pt = (unsigned int *)fw_info->info;

		while (len > 0) {
			seq_printf(s, "%08x\n", *pt);
			len -= 4;
			pt++;
		}
	} else if (fw_info->type == DUMP_RF_CAL_DATA) {
		dump_bytes(s, fw_info, 1);
	} else {
		seq_printf(s, "%s", (char *)fw_info->info);
	}

	return 0;
}
static void *fw_info_seq_next(struct seq_file *s, void *v, loff_t *pos) {return NULL; }
static void fw_info_seq_stop(struct seq_file *s, void *v) {}

static const struct seq_operations fw_info_seq_ops = {
	.start = fw_info_seq_start,
	.next  = fw_info_seq_next,
	.stop  = fw_info_seq_stop,
	.show  = fw_info_seq_show
};

/**
 * hwaddr_aton - Convert ASCII string to MAC address (colon-delimited format)
 * @txt: MAC address as a string (e.g., "00:11:22:33:44:55")
 * @addr: Buffer for the MAC address (ETH_ALEN = 6 bytes)
 * Returns: 0 on success, -1 on failure (e.g., string not a MAC address)
 */
static ssize_t proc_write_config(struct file *file,
				 const char __user *buffer,
				 size_t count,
				 loff_t *ppos)
{
	struct wifi_dev *wifi = file_inode(file)->i_private;
	char buf[80];
	unsigned long val = 0;

	if (!wifi || !wifi->hw)
		return -ENODEV;

	if (count >= sizeof(buf))
		count = sizeof(buf) - 1;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	buf[count] = '\0';

	if (param_get_val(buf, "debug_mask=", &val))
		rk915_debug_mask = val;
	else if (strstr(buf, "simulate_fw_crash"))
		/* exercise the mac80211-restart recovery path */
		rk915_signal_io_error(wifi->hal, FW_ERR_SDIO);
	else
		return -EINVAL;

	return count;
}


static int proc_open_config(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_config, inode->i_private);
}


static int proc_open_phy_stats(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_phy_stats, inode->i_private);
}

static int proc_open_mac_stats(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_mac_stats, inode->i_private);
}

static int proc_open_fw_info(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &fw_info_seq_ops);

	if (!ret)
		((struct seq_file *)file->private_data)->private =
			inode->i_private;
	return ret;
}

static int proc_open_fw_log(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &fw_log_seq_ops);

	if (!ret)
		((struct seq_file *)file->private_data)->private =
			inode->i_private;
	return ret;
}

static int proc_read_fw_params(struct seq_file *m, void *v)
{
	struct wifi_dev *wifi = m->private;
	struct fw_info_dump *fw_info = &wifi->fw_info;

	if (!wifi->hw)
		return -ENODEV;
	struct fw_params *params;
	struct img_priv *priv;

	priv = (struct img_priv *)(wifi->hw->priv);
	if (priv->state != STARTED) {
		pr_err("Interface is not initialized\n");
		goto exit;
	}

	if (rpu_fw_priv_cmd_sync(FW_GET_PARAMS, NULL) != 0)
		goto exit;

	params = (struct fw_params *)fw_info->info;
	seq_printf(m, "firmware params:\n"
				 "\techo_mode=%d\n"
				 "\tejtag_mode=%d\n"
				 "\tdebug_level=%d\n"
				 "\tdebug_flag=0x%x\n"
				 "\tdis_wifi_isr_thd=%d\n"
				 "\ten_wifi_isr_thd=%d\n",
				 params->echo_mode,
				 params->ejtag_mode,
				 params->debug_level,
				 params->debug_flag,
				 params->dis_wifi_isr_thd,
				 params->en_wifi_isr_thd);

	seq_printf(m, "\nfirmware command:\n"
				"\tread_fw_reg=0xb000c800,0x4\n"
				"\twrite_fw_reg=0xb000c800,0x80000000\n"
				"\tfw_txrx_count_info\n"
				"\tfw_txrx_queue_info\n"
				"\tfw_version_info\n"
				"\tfw_enable_ejtag\n");
	seq_printf(m, "\tdebug_level=n, n is: 0(close) 1(err) 2(info) 3(debug)\n"
			"\tdebug_flag=n, n is: 1(rx) 2(tx) 4(sdio) 8(memory)\n"
			"\techo_mode=n, n is: 0(disable) 1(enable)\n"
			"\tejtag_mode=n, n is: 0(disable) 1(enable), not available now\n"
			"\tdump_mem_info, dump firmware memory information.\n"
			);

exit:
	fw_info->finish = 0;
	return 0;
}

static ssize_t proc_write_fw_params(struct file *file,
				 const char __user *buffer,
				 size_t count,
				 loff_t *ppos)
{
	char buf[128];
	unsigned long val = 0;
	struct img_priv *priv;
	struct wifi_dev *wifi = file_inode(file)->i_private;
	int ret = 0;
	struct fw_params params;
	int cmd = 0;

	if (!wifi || !wifi->hw)
		return -ENODEV;

	priv = (struct img_priv *)(wifi->hw->priv);

	if (priv->state != STARTED) {
		pr_err("Interface is not initialized\n");
		return count;
	}

	if (count >= sizeof(buf))
		count = sizeof(buf) - 1;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	memset(&params, 0, sizeof(struct fw_params));
	buf[count] = '\0';

	pr_info("%s: %s\n", __func__, buf);

	memset(&params, 0, sizeof(struct fw_params));
	if (param_get_val(buf, "echo_mode=", &val)) {
		params.mask |= 1<<PARAM_ECHO_MODE;
		params.echo_mode = val;
	} else if (param_get_val(buf, "ejtag_mode=", &val)) {
		params.mask |= 1<<PARAM_EJTAG_MODE;
		params.ejtag_mode = val;
	} else if (param_get_val(buf, "debug_level=", &val)) {
		params.mask |= 1<<PARAM_DEBUG_LEVEL;
		params.debug_level = val;
	} else if (param_get_val(buf, "debug_flag=", &val)) {
		params.mask |= 1<<PARAM_DEBUG_FLAG;
		params.debug_flag = val;
	} else if (param_get_val(buf, "dis_wifi_isr_thd=", &val)) {
		params.mask |= 1<<PARAM_DIS_WIFI_ISR_THD;
		params.dis_wifi_isr_thd = val;
	} else if (param_get_val(buf, "en_wifi_isr_thd=", &val)) {
		params.mask |= 1<<PARAM_EN_WIFI_ISR_THD;
		params.en_wifi_isr_thd = val;
	} else if (strstr(buf, "dump_mem_info")) {
		cmd = DUMP_MEM_INFO;
	} else
		pr_err("Invalid parameter name: %s\n", buf);

	if (params.mask)
		ret = rpu_fw_priv_cmd_sync(FW_SET_PARAMS, &params);
	else
		ret = rpu_fw_priv_cmd(cmd, NULL);

	if (ret)
		return ret;

	return count;
}

static int proc_open_fw_params(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_fw_params, inode->i_private);
}

static const struct file_operations params_fops_config = {
	.open = proc_open_config,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = proc_write_config,
	.release = single_release
};
static const struct file_operations params_fops_phy_stats = {
	.open = proc_open_phy_stats,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = NULL,
	.release = single_release
};
static const struct file_operations params_fops_mac_stats = {
	.open = proc_open_mac_stats,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = NULL,
	.release = single_release
};
static const struct file_operations params_fops_fw_info = {
	.open = proc_open_fw_info,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = NULL,
	.release = seq_release
};
static const struct file_operations params_fops_fw_log = {
	.open = proc_open_fw_log,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = NULL,
	.release = seq_release
};
static const struct file_operations params_fops_fw_params = {
	.open = proc_open_fw_params,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = proc_write_fw_params,
	.release = single_release
};

static void set_default_phy_thresh(unsigned char *rf_params, int len)
{
	int i;
	unsigned char def[3];

	sprintf(def, "%02x", default_phy_threshold);

	for (i = 0; i < 14; i++)
		memcpy(&rf_params[(len - i)*2 - 2], def, 2);
}

void set_rf_params(struct wifi_dev *wifi, unsigned char *rf_params)
{
	if (!wifi)
		return;

	set_default_phy_thresh(rf_params, 94);

	memset(wifi->params.rf_params, 0xFF, sizeof(wifi->params.rf_params));
	conv_str_to_byte(wifi->params.rf_params, rf_params, RF_PARAMS_SIZE);

	memcpy(wifi->params.rf_params_vpd, wifi->params.rf_params, RF_PARAMS_SIZE);
}

void rk915_debugfs_init(struct wifi_dev *wifi)
{
	wifi->umac_proc_dir_entry = debugfs_create_dir("rk915",
					wifi->hw->wiphy->debugfsdir);

	debugfs_create_file("params", 0644, wifi->umac_proc_dir_entry,
			    wifi, &params_fops_config);
	debugfs_create_file("phy_stats", 0444, wifi->umac_proc_dir_entry,
			    wifi, &params_fops_phy_stats);
	debugfs_create_file("mac_stats", 0444, wifi->umac_proc_dir_entry,
			    wifi, &params_fops_mac_stats);
	debugfs_create_file("sleep_stats", 0444, wifi->umac_proc_dir_entry,
			    wifi, &params_fops_sleep_stats);
	debugfs_create_file("fw_info", 0444, wifi->umac_proc_dir_entry,
			    wifi, &params_fops_fw_info);
	debugfs_create_file("fw_log", 0444, wifi->umac_proc_dir_entry,
			    wifi, &params_fops_fw_log);
	debugfs_create_file("fw_params", 0644, wifi->umac_proc_dir_entry,
			    wifi, &params_fops_fw_params);
}

struct dentry *rk915_debugfs_dir(struct wifi_dev *wifi)
{
	return wifi ? wifi->umac_proc_dir_entry : NULL;
}

struct wifi_dev *proc_init(struct hal_priv *hal)
{
	struct wifi_dev *wifi;
	unsigned int i = 0;
	/*2.4GHz and 5 GHz PD and TX-PWR calibration params*/
	unsigned char rf_params[RF_PARAMS_SIZE * 2];

	//strncpy(rf_params,
	//	"1E00000000002426292A2C2E3237393F454A52576066000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F00000000002426292A2C2E3237393F454A52576066000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F0808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808",
	//	(RF_PARAMS_SIZE * 2));

	//From above check PHY Start Thresholds in the last 14 bytes (corerspond to all 14 channels). Each value specifies the threshold in half-dBm units without the negative sign.
	//The default is 0xB4 = 180, which represents -90 dBm.
	memcpy(rf_params, "00204000240210020403040404050406040704080409040A040B040C040CF80B0C04FFFAF2EC0000000000000000000000000000000000000000000000000000000008080808080808080808080808088C8C8C8C8C8C8C8C8C8C8C8C8C8C",
	       94 * 2);
	set_default_phy_thresh(rf_params, 94);

	wifi = kzalloc_obj(struct wifi_dev, GFP_KERNEL);
	if (!wifi) {
		goto out;
	}

	/* Initialize WLAN params */
	memset(&wifi->params, 0, sizeof(struct wifi_params));

	memset(wifi->params.rf_params, 0xFF, sizeof(wifi->params.rf_params));
	conv_str_to_byte(wifi->params.rf_params, rf_params, RF_PARAMS_SIZE);

	if (!rf_params_vpd)
		rf_params_vpd = wifi->params.rf_params;

	memcpy(wifi->params.rf_params_vpd, rf_params_vpd, RF_PARAMS_SIZE);

	wifi->params.is_associated = 0;
	wifi->params.ed_sensitivity = -89;
	wifi->params.auto_sensitivity = 1;
	wifi->params.dot11a_support = 0;
	wifi->params.dot11g_support = 1;
	wifi->params.num_vifs = 2;

	/* Check, if required add it */
	wifi->params.tx_fixed_mcs_indx = -1;
	wifi->params.tx_fixed_rate = -1;
	wifi->params.num_spatial_streams = min(MAX_TX_STREAMS, MAX_RX_STREAMS);
	wifi->params.uccp_num_spatial_streams = min(MAX_TX_STREAMS,
							MAX_RX_STREAMS);
	wifi->params.antenna_sel = 1;

	if (num_streams_vpd > 0)
		wifi->params.uccp_num_spatial_streams = num_streams_vpd;

	wifi->params.enable_early_agg_checks = 1;
	wifi->params.bt_state = 1;

	/* Defaults optimized for all clients
	 */
	wifi->params.mgd_mode_tx_fixed_mcs_indx = -1;
	wifi->params.mgd_mode_mcast_fixed_data_rate = -1;
	wifi->params.mgd_mode_tx_fixed_rate = -1;
	wifi->params.mgd_mode_mcast_fixed_nss = 1;
	wifi->params.mgd_mode_mcast_fixed_bcc_or_ldpc = 1;
	wifi->params.mgd_mode_mcast_fixed_stbc_enabled = 1;
	wifi->params.chnl_bw = WLAN_20MHZ_OPERATION;

	wifi->params.max_tx_streams = MAX_TX_STREAMS;
	wifi->params.max_rx_streams = MAX_RX_STREAMS;
	wifi->params.max_data_size  = 8 * 1024;

	wifi->params.max_tx_cmds = MAX_SUBFRAMES_IN_AMPDU_HT;
	wifi->params.disable_power_save = 0;
	wifi->params.disable_sm_power_save = 0;
	wifi->params.rate_protection_type = 0;
	wifi->params.prod_mode_rate_preamble_type = 1; /* LONG */
	wifi->params.prod_mode_stbc_enabled = 0;
	wifi->params.prod_mode_bcc_or_ldpc = 0;
	wifi->params.bg_scan_enable = 0;
	memset(wifi->params.bg_scan_channel_list, 0, 50);
	memset(wifi->params.bg_scan_channel_flags, 0, 50);

	if (wifi->params.dot11g_support) {
		wifi->params.bg_scan_num_channels = 3;

		wifi->params.bg_scan_channel_list[i] = 1;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;

		wifi->params.bg_scan_channel_list[i] = 6;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;

		wifi->params.bg_scan_channel_list[i] = 11;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;
	}

	wifi->params.disable_beacon_ibss = 0;
	wifi->params.fw_skip_rx_pkt_submit = 0;
	wifi->params.bg_scan_intval = 5000 * 1000; /* Once in 5 seconds */
	wifi->params.bg_scan_chan_dur = 300; /* Channel spending time */
	wifi->params.bg_scan_serv_chan_dur = 100; /* Oper chan spending time */
	wifi->params.nw_selection = 0;
	wifi->params.scan_type = ACTIVE;
	wifi->params.hw_scan_status = HW_SCAN_STATUS_NONE;
	wifi->params.fw_loaded = 0;
	/* Default RPU Sleep is enabled
	 */
	if (lpw_no_sleep)
		wifi->params.rpu_sleep_type = LMAC_NO_SLEEP;
	else
		wifi->params.rpu_sleep_type = 0;

#ifdef ENABLE_DAPT
	wifi->params.dapt_thresh_offset = DAPT_THRESH_OFFSET;
	wifi->params.dapt_thresh_exponent = DAPT_THRESH_EXPONENT;
	wifi->params.dapt_thresh_min = DAPT_PHY_THRESH_MIN;
	wifi->params.dapt_thresh_max = DAPT_PHY_THRESH_MAX;
#endif

	wifi->params.min_dtim_peroid = 4;

	memset(&wifi->fw_info, 0, sizeof(struct fw_info_dump));
	wifi->fw_info.info = kzalloc(MAX_FW_INFO_SIZE, GFP_KERNEL);
	if (!wifi->fw_info.info)
		goto fw_info_fail;
	wifi->fw_info.len = MAX_FW_INFO_SIZE;
	wifi->fw_info.offset = 0;

	wifi->hal = hal;
	hal->wifi = wifi;

	return wifi;
fw_info_fail:
	kfree(wifi);
out:
	return NULL;

}

void proc_exit(struct wifi_dev *wifi)
{
	if (!wifi)
		return;

	/* debugfs entries go away with the wiphy debugfs dir */
	wifi->hal->wifi = NULL;
	kfree(wifi->fw_info.info);
	kfree(wifi);
}


