// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021, Fuzhou Rockchip Electronics Co., Ltd
 */

#include "core.h"
#include "utils.h"

void init_vif_info(struct img_priv *priv)
{
	struct vif_info_s *vif_info = &priv->vif_info;

	memset(vif_info, 0, sizeof(struct vif_info_s));
}

bool is_wlan_connected(struct img_priv *priv)
{
	struct vif_info_s *vif_info = &priv->vif_info;
	int idx = find_main_iface(priv);

	if (idx > MAX_VIFS - 1)
		return false;

	if (vif_info->conn_state[idx])
		return true;
	return false;
}

bool is_p2p_connected(struct img_priv *priv)
{
	struct vif_info_s *vif_info = &priv->vif_info;
	int idx = find_main_iface(priv);

	if (idx > MAX_VIFS - 1)
		return false;

	if (vif_info->conn_state[idx])
		return true;
	return false;
}

static void notify_bssid_change(struct img_priv *priv,
					int index,
					unsigned char *vif_addr,
					unsigned char *bssid)
{
	struct vif_info_s *vif_info = &priv->vif_info;

	rk915_dbg(RK915_DBG_VIF, "%s: index = %d, vif_addr = %pM, bssid = %pM\n",
			__func__, index, vif_addr, bssid);

	if (index > MAX_VIFS - 1)
		return;

	if (vif_addr)
		memcpy(vif_info->vif_addr[index], vif_addr, ETH_ALEN);
	else
		memset(vif_info->vif_addr[index], 0, ETH_ALEN);
	if (bssid)
		memcpy(vif_info->bssid[index], bssid, ETH_ALEN);
	else
		memset(vif_info->bssid[index], 0, ETH_ALEN);
}

static void  notify_conn_state(struct img_priv *priv,
					int index,
					unsigned char *vif_addr,
					unsigned int connect_state)
{
	struct vif_info_s *vif_info = &priv->vif_info;

	rk915_dbg(RK915_DBG_VIF, "%s: index = %d, vif_addr = %pM, connect_state = %s\n",
			__func__, index, vif_addr, connect_state == STA_CONN ? "CONN":"DISCONN");

	if (index > MAX_VIFS - 1)
		return;

	memcpy(vif_info->vif_addr[index], vif_addr, ETH_ALEN);
	if (connect_state == STA_CONN)
		vif_info->conn_state[index] = 1;
	else
		vif_info->conn_state[index] = 0;
}

void rpu_vif_add(struct umac_vif *uvif)
{
	unsigned int type;
	struct ieee80211_conf *conf = &uvif->priv->hw->conf;
	int ret = 0;

	rk915_dbg(RK915_DBG_VIF, "%s-UMAC: Add VIF %d Type = %d\n",
		   uvif->priv->name,
		   uvif->vif_index,
		   uvif->vif->type);

	uvif->config.atim_window = uvif->config.bcn_lost_cnt =
		uvif->config.aid = 0;

	switch (uvif->vif->type) {
	case NL80211_IFTYPE_STATION:
		type = IF_MODE_STA_BSS;
		uvif->noa_active = 0;
		skb_queue_head_init(&uvif->noa_que);
		spin_lock_init(&uvif->noa_que.lock);
		break;
	case NL80211_IFTYPE_ADHOC:
		type = IF_MODE_STA_IBSS;
		init_beacon(uvif);
		break;
	case NL80211_IFTYPE_AP:
		type = IF_MODE_AP;
		init_beacon(uvif);
		break;
	default:
		WARN_ON(1);
		return;
	}

	ret = rpu_prog_vif_ctrl(uvif->vif_index, uvif->vif->addr, type, IF_ADD);
	if (ret != 0)
		goto prog_rpu_fail;

#ifdef ENABLE_DAPT
	dapt_notify_bssid_change(uvif->priv,
			uvif->vif_index,
			uvif->vif->addr,
			NULL);
#endif
	notify_bssid_change(uvif->priv,
			uvif->vif_index,
			uvif->vif->addr,
			NULL);

	/* Reprogram retry counts */
	ret = rpu_prog_short_retry(uvif->vif_index, uvif->vif->addr, conf->short_frame_max_tx_count);
	if (ret != 0)
		goto prog_rpu_fail;

	ret = rpu_prog_long_retry(uvif->vif_index, uvif->vif->addr, conf->long_frame_max_tx_count);
	if (ret != 0)
		goto prog_rpu_fail;

	if (uvif->vif->type == NL80211_IFTYPE_AP) {
		/* Program the EDCA params */
		unsigned int queue;
		unsigned int aifs;
		unsigned int txop;
		unsigned int cwmin;
		unsigned int cwmax;
		unsigned int uapsd;

		for (queue = 0; queue < 4; queue++) {
			aifs = uvif->config.edca_params[queue].aifs;
			txop = uvif->config.edca_params[queue].txop;
			cwmin = uvif->config.edca_params[queue].cwmin;
			cwmax = uvif->config.edca_params[queue].cwmax;
			uapsd = uvif->config.edca_params[queue].uapsd;
			rk915_dbg(RK915_DBG_VIF, "%s: queue=%d, aifs=%d, txop=%d, cwmin=%d, cwmax=%d, uapsd=%d\n",
							__func__, queue, aifs, txop, cwmin, cwmax, uapsd);

			ret = rpu_prog_txq_params(uvif->vif_index, uvif->vif->addr, queue, aifs, txop, cwmin, cwmax, uapsd);
			if (ret != 0)
				goto prog_rpu_fail;
		}
	}
prog_rpu_fail:
	return;
}


void rpu_vif_remove(struct umac_vif *uvif)
{
	struct sk_buff *skb;
	unsigned int type;
	int ret = 0;

	rk915_dbg(RK915_DBG_VIF, "%s-UMAC: Remove VIF %d called\n",
					uvif->priv->name,
					uvif->vif_index);

	switch (uvif->vif->type) {
	case NL80211_IFTYPE_STATION:
		type = IF_MODE_STA_BSS;
		break;
	case NL80211_IFTYPE_ADHOC:
		type = IF_MODE_STA_IBSS;
		deinit_beacon(uvif);
		break;
	case NL80211_IFTYPE_AP:
		type = IF_MODE_AP;
		deinit_beacon(uvif);
		break;
	default:
		WARN_ON(1);
		return;
	}


	if (type == IF_MODE_STA_BSS) {
		spin_lock_bh(&uvif->noa_que.lock);

		while ((skb = __skb_dequeue(&uvif->noa_que)))
			dev_kfree_skb(skb);

		spin_unlock_bh(&uvif->noa_que.lock);
	}

	ret = rpu_prog_vif_ctrl(uvif->vif_index, uvif->vif->addr, type, IF_REM);
	if (ret != 0)
		goto prog_rpu_fail;

#ifdef ENABLE_DAPT
	dapt_notify_bssid_change(uvif->priv,
			uvif->vif_index,
			NULL,
			NULL);
#endif
	notify_bssid_change(uvif->priv,
			uvif->vif_index,
			NULL,
			NULL);

prog_rpu_fail:
	return;
}


void rpu_vif_set_edca_params(unsigned short queue,
					struct umac_vif *uvif,
					struct edca_params *params,
					unsigned int vif_active)
{
	int ret = 0;

	switch (queue) {
	case 0:
		queue = 3; /* Voice */
		break;
	case 1:
		queue = 2; /* Video */
		break;
	case 2:
		queue = 1; /* Best effort */
		break;
	case 3:
		queue = 0; /* Back groud */
		break;
	}

	rk915_dbg(RK915_DBG_VIF, "%s-UMAC:Set EDCA params for VIF %d,",
		   uvif->priv ? uvif->priv->name : 0, uvif->vif_index);
	rk915_dbg(RK915_DBG_VIF, " Values: %d, %d, %d, %d, %d\n",
		   queue, params->aifs, params->txop,
		   params->cwmin, params->cwmax);

	{
		/* arbitration interframe space [0..255] */
		uvif->config.edca_params[queue].aifs = params->aifs;

		/* maximum burst time in units of 32 usecs, 0 meaning disabled*/
		uvif->config.edca_params[queue].txop = params->txop;

		/* minimum contention window in units of  2^n-1 */
		uvif->config.edca_params[queue].cwmin = params->cwmin;

		/*  maximum contention window in units of 2^n-1 */
		uvif->config.edca_params[queue].cwmax = params->cwmax;
		uvif->config.edca_params[queue].uapsd = params->uapsd;
	}

	/* For the AP case, EDCA params are set before ADD interface is called.
	 * Since this is not supported, we simply store the params and program
	 * them to the LMAC after the interface is added
	 */
	if (!vif_active) {
		WARN_ON(1);
		return;
	}

	/* Program the txq parameters into the LMAC */
	ret = rpu_prog_txq_params(uvif->vif_index, uvif->vif->addr, queue, params->aifs, params->txop, params->cwmin, params->cwmax, params->uapsd);
	if (ret != 0)
		goto prog_rpu_fail;
prog_rpu_fail:
	return;
}


void rpu_vif_bss_info_changed(struct umac_vif *uvif, struct ieee80211_vif *vif,
					struct ieee80211_hw *hw,
					struct ieee80211_bss_conf *bss_conf,
					unsigned int changed)
{
	unsigned int caps = 0;
	int center_freq = 0;
	int chan = 0;
	int ret = 0;

	rk915_dbg(RK915_DBG_VIF, "%s-CORE: BSS INFO changed %d, %d, %d\n",
		uvif->priv->name, uvif->vif_index, uvif->vif->type, changed);


	if (changed & BSS_CHANGED_BSSID) {
		ret = rpu_prog_vif_bssid(uvif->vif_index, uvif->vif->addr, (unsigned char *)bss_conf->bssid);
		if (ret != 0)
			goto prog_rpu_fail;
#ifdef ENABLE_DAPT
		dapt_notify_bssid_change(uvif->priv,
			   uvif->vif_index,
			   uvif->vif->addr,
			   (unsigned char *)bss_conf->bssid);
#endif
		notify_bssid_change(uvif->priv,
			   uvif->vif_index,
			   uvif->vif->addr,
			   (unsigned char *)bss_conf->bssid);
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		if (bss_conf->basic_rates)
			ret = rpu_prog_vif_basic_rates(uvif->vif_index,
						       uvif->vif->addr,
						       bss_conf->basic_rates);
		else
			ret = rpu_prog_vif_basic_rates(uvif->vif_index,
						       uvif->vif->addr, 0x153);
		if (ret != 0)
			goto prog_rpu_fail;
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		unsigned int queue = 0;
		unsigned int aifs = 0;
		unsigned int txop = 0;
		unsigned int cwmin = 0;
		unsigned int cwmax = 0;
		unsigned int uapsd = 0;

		ret = rpu_prog_vif_short_slot(uvif->vif_index, uvif->vif->addr, bss_conf->use_short_slot);
		if (ret != 0)
			goto prog_rpu_fail;

		for (queue = 0; queue < WLAN_AC_MAX_CNT; queue++) {
			aifs = uvif->config.edca_params[queue].aifs;
			txop = uvif->config.edca_params[queue].txop;
			cwmin = uvif->config.edca_params[queue].cwmin;
			cwmax = uvif->config.edca_params[queue].cwmax;
			uapsd = uvif->config.edca_params[queue].uapsd;

			if (uvif->config.edca_params[queue].cwmin != 0) {
				ret = rpu_prog_txq_params(uvif->vif_index,
							  uvif->vif->addr,
							  queue, aifs, txop,
							  cwmin, cwmax, uapsd);
				if (ret != 0)
					goto prog_rpu_fail;
			}
		}
	}

	switch (uvif->vif->type) {
	case NL80211_IFTYPE_STATION:
		if (changed & BSS_CHANGED_ASSOC) {
			if (vif->cfg.assoc) {
				rk915_dbg(RK915_DBG_VIF, "%s-CORE: AID %d,",
					   uvif->priv->name, vif->cfg.aid);
				rk915_dbg(RK915_DBG_VIF, " CAPS 0x%04x\n",
					   bss_conf->assoc_capability |
					   (bss_conf->qos << 9));

				ret = rpu_prog_vif_conn_state(uvif->vif_index, uvif->vif->addr, STA_CONN);
				if (ret != 0)
					goto prog_rpu_fail;
#ifdef ENABLE_DAPT
				dapt_notify_conn_state(uvif->priv,
					  uvif->vif_index,
					  uvif->vif->addr,
					  STA_CONN);
#endif
				notify_conn_state(uvif->priv,
					  uvif->vif_index,
					  uvif->vif->addr,
					  STA_CONN);
				ret = rpu_prog_vif_aid(uvif->vif_index, uvif->vif->addr, vif->cfg.aid);
				if (ret != 0)
					goto prog_rpu_fail;

				center_freq = hw->conf.chandef.chan->center_freq;
				chan = ieee80211_frequency_to_channel(center_freq);
				ret = rpu_prog_vif_op_channel(uvif->vif_index, uvif->vif->addr, chan);
				if (ret != 0)
					goto prog_rpu_fail;

				caps = (bss_conf->assoc_capability |
					(bss_conf->qos << 9));

				ret = rpu_prog_vif_assoc_cap(uvif->vif_index, uvif->vif->addr, caps);
				if (ret != 0)
					goto prog_rpu_fail;


				uvif->noa_active = 0;
				uvif->priv->params->is_associated = 1;

			} else {
				uvif->priv->params->is_associated = 0;

				ret = rpu_prog_vif_conn_state(uvif->vif_index, uvif->vif->addr, STA_DISCONN);
				if (ret != 0)
					goto prog_rpu_fail;
#ifdef ENABLE_DAPT
				dapt_notify_conn_state(uvif->priv,
					  uvif->vif_index,
					  uvif->vif->addr,
					  STA_DISCONN);
#endif
				notify_conn_state(uvif->priv,
					  uvif->vif_index,
					  uvif->vif->addr,
					  STA_DISCONN);
			}
		}

		if (changed & BSS_CHANGED_BEACON_INT) {
			ret = rpu_prog_vif_beacon_int(uvif->vif_index, uvif->vif->addr, bss_conf->beacon_int);
			if (ret != 0)
				goto prog_rpu_fail;

		}

		if (changed & BSS_CHANGED_BEACON_INFO) {
			ret = rpu_prog_vif_dtim_period(uvif->vif_index, uvif->vif->addr, bss_conf->dtim_period);
			if (ret != 0)
				goto prog_rpu_fail;

		}

		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
		if ((changed & BSS_CHANGED_BEACON_ENABLED) ||
			(changed & BSS_CHANGED_BEACON_INT))
			modify_beacon_params(uvif, bss_conf);
		break;
	default:
		WARN_ON(1);
		return;
	}
prog_rpu_fail:
	return;
}


