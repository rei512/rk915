// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <net/mac80211.h>
#include <linux/time.h>
#include <linux/pm.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/reboot.h>
#include "hal_common.h"
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/delay.h>

#include "core.h"
#include "if_io.h"
#include "hal.h"
#include "utils.h"
#include "platform.h"
#include "hal_io.h"

bool m0_jtag_enable;

static int fw_bring_up(void *p)
{
	struct hal_priv *priv = (struct hal_priv *)p;
	struct sk_buff *skb;

	if (priv->fw_error_processing) {
		if (rk915_sdio_power_cycle(priv->io_info)) {
			rk915_err("%s: power cycle failed\n", __func__);
			return -1;
		}
	} else {
		if (rk915_platform_bus_init(priv->io_info)) {
			rk915_err("%s: platform_bus_init failed\n", __func__);
			return -1;
		}

		/* Firmware can only be loaded into fresh ROM: power-cycle
		 * in case the chip is already running an image.
		 */
		if (rk915_sdio_power_cycle(priv->io_info)) {
			rk915_err("%s: power cycle failed\n", __func__);
			return -1;
		}

		/* The chip is fresh: drop state left over from the
		 * previous session, otherwise the tx path stays gated
		 * and no command ever reaches the firmware.
		 */
		rk915_irq_enable(priv, 1);
		if (block_rpu_comm) {
			rk915_dbg(RK915_DBG_MAIN, "%s: clearing stale block_rpu_comm\n",
					__func__);
			block_rpu_comm = false;
		}
		if (priv->fw_error || priv->fw_error_processing) {
			rk915_dbg(RK915_DBG_MAIN, "%s: clearing stale fw error state\n",
					__func__);
			priv->fw_error = 0;
			priv->fw_error_processing = 0;
			rk915_wake_waiters(priv);
		}
		while ((skb = skb_dequeue(&priv->txq)))
			dev_kfree_skb_any(skb);
	}

	if (rk915_download_firmware(priv)) {
		rk915_err("%s: rk915_download_firmware failed\n", __func__);
		return -1;
	}

	if (rk915_io_init(priv)) {
		rk915_err("%s: rk915_io_init failed\n", __func__);
		return -1;
	}

	if (!down_fw_in_probe && !priv->fw_error_processing) {
		if (rk915_register_irq(priv->io_info)) {
			rk915_err("%s: rk915_irq_register failed\n", __func__);
			return -1;
		}
	}

	return 0;
}

static int fw_tear_down(void *p)
{
	/* The SDIO bus is owned by probe/remove and the chip is
	 * power-cycled by the next bring-up, so there is nothing to
	 * undo here.
	 */
	return 0;
}

static int rk915_reboot_notify(struct notifier_block *nb,
				unsigned long action, void *data)
{
	struct hal_priv *priv = container_of(nb, struct hal_priv, reboot_nb);

	priv->shutdown = 1;

	return NOTIFY_DONE;
}

void rk915_core_deinit(struct hal_priv *priv)
{
	struct host_io_info *host;

	if (!priv)
		return;

	unregister_reboot_notifier(&priv->reboot_nb);

	host = priv->io_info;
	if (host)
		rk915_free_firmware_buf(&host->firmware);
	if (host && host->rx_serias_buf)
		kfree(host->rx_serias_buf);
	kfree(priv);
	kfree(host);
}

struct hal_priv *rk915_core_init(void)
{
	struct host_io_info *host = NULL;
	struct hal_priv *priv = NULL;


	host = kzalloc(sizeof(struct host_io_info), GFP_KERNEL);
	if (!host)
		goto err;

	host->rx_serias_buf = kzalloc(MAX_RX_SERIAS_BYTES, GFP_KERNEL);
	if (!host->rx_serias_buf)
		goto err;

	host->rx_serias_idx = -1;
	host->rx_serias_count = 0;
	host->rx_next_len = 0;
	host->bus_init = false;

	if (rk915_alloc_firmware_buf(&host->firmware) != 0)
		goto err;

	priv = kzalloc(sizeof(struct hal_priv), GFP_KERNEL);
	if (!priv)
		goto err;

	priv->io_info = host;
	host->hal = priv;
	init_waitqueue_head(&priv->wait_q);

	priv->reboot_nb.notifier_call = rk915_reboot_notify;
	register_reboot_notifier(&priv->reboot_nb);

	return priv;

err:
	if (host) {
		rk915_free_firmware_buf(&host->firmware);
		kfree(host->rx_serias_buf);
	}
	kfree(host);

	return NULL;
}

int rk915_device_probe(struct hal_priv *priv)
{
	int ret;

	priv->fw_bring_up_func = fw_bring_up;
	priv->fw_tear_down_func = fw_tear_down;

	/* Initialize the rest of the layer */
	ret = hal_ops.init(priv);
	if (ret < 0) {
		rk915_err("%s: hal_ops.init failed\n", __func__);
		return -1;
	}

	if (down_fw_in_probe) {
		if (fw_bring_up(priv)) {
			rk915_err("%s: fw_bring_up failed\n", __func__);
			goto err_deinit;
		}

		if (rk915_register_irq(priv->io_info)) {
			rk915_err("%s: rk915_irq_register failed\n", __func__);
			goto err_deinit;
		}
	}

	return 0;

err_deinit:
	priv->shutdown = 1;
	hal_ops.deinit(priv);
	return -1;
}

void rk915_device_remove(struct hal_priv *priv)
{

	/* stop fw recovery before teardown: it power-cycles the card */
	priv->shutdown = 1;

	hal_ops.deinit(priv);

	rk915_free_irq(priv->io_info);

	rk915_platform_bus_deinit(priv->io_info);
}

static int __init rk915_init(void)
{
	int ret;

	rk915_info("driver version %s\n", VERSION_INFO);

	ret = rk915_bus_register_driver();

	return ret;
}

static void __exit rk915_exit(void)
{
	rk915_bus_unregister_driver();
}

module_init(rk915_init);
module_exit(rk915_exit);

MODULE_AUTHOR("Rockchips");
MODULE_DESCRIPTION("Driver for Rockchips RK915 SDIO WiFi Devices");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(RK915_FW_FILE);
MODULE_FIRMWARE(RK915_PATCH_FILE);
