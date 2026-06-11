// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021, Fuzhou Rockchip Electronics Co., Ltd
 */

#include "wow.h"
#include "core.h"
#include "hal_io.h"

#ifdef CONFIG_PM

unsigned char img_suspend_status;

static int wait_for_all_cmd_done(struct img_priv *priv)
{
	wait_event_timeout(priv->hal->wait_q, !cmd_info.outstanding_ctrl_req,
			   msecs_to_jiffies(1000));

	if (cmd_info.outstanding_ctrl_req) {
		rk915_dbg(RK915_DBG_UMACIF, "%s: Failed to wait all cmd done\n",
			__func__);
		return -1;
	}

	rk915_dbg(RK915_DBG_UMACIF, "%s : All cmd done\n",
						__func__);
	return 0;
}

int img_resume(struct ieee80211_hw *hw)
{
	//int i = 0, ret = 0;
	//int active_vif_index = -1;
	struct img_priv *priv = NULL;

	if (hw == NULL) {
		rk915_err("%s: Invalid parameters\n",
			__func__);
		return -EINVAL;
	}

	priv = (struct img_priv *)hw->priv;

	if (!priv->params->is_associated) {
		rk915_notify_pm(priv->hal, 1);
		return 0;
	}

	rk915_notify_pm(priv->hal, 1);
	hal_ops.disable_irq_wake(priv->hal);
	img_suspend_status = 0;

	return 0;
}

int img_suspend(struct ieee80211_hw *hw,
			struct cfg80211_wowlan *wowlan)
{
	int i = 0, ret = 0;
	int active_vif_index = -1;
	int count = 0;
	struct img_priv *priv = NULL;
	struct ieee80211_vif *vif = NULL;

	if (hw == NULL) {
		rk915_err("%s: Invalid parameters\n",
			__func__);
		return -EINVAL;
	}

	priv = (struct img_priv *)hw->priv;

	if (!priv->params->is_associated) {
		rk915_notify_pm(priv->hal, 0);
		return ret;
	}

	if ((priv->params->hw_scan_status == HW_SCAN_STATUS_PROGRESS) ||
		(priv->roc_params.roc_starting == 1))
		return -EBUSY;


	/* Outstanding commands are not drained here; system suspend has
	 * not been validated on the supported boards.
	 */

	mutex_lock(&priv->mutex);

	for (i = 0; i < MAX_VIFS; i++) {
		if (priv->active_vifs & (1 << i)) {
			active_vif_index = i;
			count++;
		}
	}

	if (count != 1) {
		rk915_err("%s: Economy mode supp only for single VIF(STA mode)\n",
			__func__);
		mutex_unlock(&priv->mutex);
		return -ENOTSUPP;
	}

	rcu_read_lock();
	vif = rcu_dereference(priv->vifs[active_vif_index]);
	rcu_read_unlock();

	if (vif->type != NL80211_IFTYPE_STATION) {
		rk915_err("%s: VIF is not in STA Mode\n",
			__func__);
		mutex_unlock(&priv->mutex);
		return -ENOTSUPP;
	}

	if (priv->power_save == PWRSAVE_STATE_AWAKE) {
		priv->power_save = PWRSAVE_STATE_DOZE;
		rpu_prog_ps_state(active_vif_index, vif->addr, priv->power_save);
		if (wait_for_all_cmd_done(priv) != 0) {
			mutex_unlock(&priv->mutex);
			return -EBUSY;
		}
	}

	mutex_unlock(&priv->mutex);

	rk915_notify_pm(priv->hal, 0);
	hal_ops.enable_irq_wake(priv->hal);
	img_suspend_status = 1;

	return 0;
}


#endif


