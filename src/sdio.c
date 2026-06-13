// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008 -2014 Rockchip System.
 */
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
#include <linux/rfkill.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/delay.h>

#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>

#include "core.h"
#include "if_io.h"
#include "sdio.h"
#include "hal.h"
#include "utils.h"
#include "platform.h"



#define MANUFACTURER_ID_EAGLE_BASE        0x5347
#define MANUFACTURER_ID_EAGLE_BASEX       0x5348
#define MANUFACTURER_CODE                 0x296


static const struct sdio_device_id rk915_sdio_devices[] = {
	{SDIO_DEVICE(MANUFACTURER_CODE, MANUFACTURER_ID_EAGLE_BASE)},
	{SDIO_DEVICE(MANUFACTURER_CODE, MANUFACTURER_ID_EAGLE_BASEX)},
	{},
};
MODULE_DEVICE_TABLE(sdio, rk915_sdio_devices);

struct device *hal_get_dev(struct hal_priv *priv)
{
	struct sdio_func *func = priv->io_info->priv_data;

	return &func->dev;
}

//extern u32 mmc_debug_level;
extern int sdio_reset_comm(struct mmc_card *card);
//static unsigned char resetdata[1024];
static bool sdio_reset;
int _sdio_reset(struct host_io_info *host)
{
	sdio_reset = true;
	return 0;
}

#if SUPPORT_SDIO_SLEEP
int lpw_is_ready;
static int is_sdio_sleep;

int sdio_clk_sleep(struct host_io_info *host, int val)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;
	struct mmc_host *shost = func->card->host;
	static int mmc_working_clk;

	rk915_dbg(RK915_DBG_SDIO, "%s: %d\n", __func__, val);

	// 1. config sdio clock for save power
	if (val) {
		// backup working clock, and set sdio clock to 0
		if (shost->ios.clock > 0)
			mmc_working_clk = shost->ios.clock;
		shost->ios.clock = 0;
	} else {
		// restore sdio clock
		shost->ios.clock = mmc_working_clk;
	}
	if (shost->ios.clock > shost->f_max)
		shost->ios.clock = shost->f_max;
	rk915_dbg(RK915_DBG_SDIO, "%s: change clock to %d\n", __func__, shost->ios.clock);

	// 2. config sdio pin ctrl for save power
#define MMC_POWER_CLK_SLEEP	10
#define MMC_POWER_CLK_WAKEUP	11
	shost->ios.power_mode = val?MMC_POWER_CLK_SLEEP:MMC_POWER_CLK_WAKEUP;
	rk915_dbg(RK915_DBG_SDIO, "%s: change power mode %d\n", __func__, shost->ios.power_mode);
	shost->ops->set_ios(shost, &shost->ios);

	usleep_range(5000, 6000);

	return 0;
}

// change sdio clock to zero and iomux to gpio
static int sdio_sleep(struct host_io_info *host)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;

	if (lpw_is_ready == 0) {
		rk915_err("%s: LPW is not ready!\n", __func__);
		return 0;
	}

	sdio_claim_host(func);

	if (is_sdio_sleep == 0) {
		sdio_clk_sleep(host, 1);
		is_sdio_sleep = 1;
	}

	sdio_release_host(func);

	return 0;
}

static int sdio_wakeup_unlock(struct host_io_info *host)
{
	if (is_sdio_sleep) {
		sdio_clk_sleep(host, 0);
		is_sdio_sleep = 0;
	}

	return 0;
}

static int sdio_wakeup(struct host_io_info *host)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;

	sdio_claim_host(func);
	sdio_wakeup_unlock(host);
	sdio_release_host(func);

	return 0;
}

#if SDIO_AUTO_SLEEP
static void sleep_timer_expiry(struct work_struct *work)
{
	struct host_io_info *host =
		container_of(work, struct host_io_info, sleep_work.work);

	sdio_sleep(host);
}
#endif
#endif

static void sdio_lock(struct host_io_info *host)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;

#if SDIO_AUTO_SLEEP
	cancel_delayed_work_sync(&host->sleep_work);
#endif
	sdio_claim_host(func);
#if SDIO_AUTO_SLEEP
	sdio_wakeup_unlock(host);
#endif
}

static void sdio_unlock(struct host_io_info *host)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;

#if SDIO_AUTO_SLEEP
	#define SLEEP_TIMEOUT_MS	100
	schedule_delayed_work(&host->sleep_work, HZ/(1000/SLEEP_TIMEOUT_MS));
#endif
	sdio_release_host(func);
}

static int _sdio_readb(struct host_io_info *host, u32 addr)
{
	int val, error;
	struct sdio_func *func = (struct sdio_func *)host->priv_data;

	if (sdio_reset == true)
		return 0;

	val = sdio_readb(func, addr, &error);

	if (val == 0xff)
		return error;
	else
		return val;
}

static int _sdio_writeb(struct host_io_info *host, u32 addr, u8 val)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;
	int error;

	if (sdio_reset == true)
		return 0;

	sdio_writeb(func, val, addr, &error);
	return error;
}

static int sdio_send_data_sg(struct host_io_info *host, u32 addr, u8 *buf, u32 len)
{
	int ret = 0;

	if (sdio_reset == true)
		return 0;

	return ret;
}

static int sdio_send_data(struct host_io_info *host, u32 addr, u8 *buf, u32 len)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;

	if (sdio_reset == true)
		return 0;

	return sdio_memcpy_toio(func, addr, buf, len);
}

static int sdio_recv_data(struct host_io_info *host, u32 addr, u8 *buf, u32 len)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;

	if (sdio_reset == true)
		return 0;

	return sdio_memcpy_fromio(func, buf, addr, len);
}

/*
 * Devices that remain active during a system suspend are
 * put back into 1-bit mode.
 */
static int sdio_disable_wide(struct host_io_info *host)
{
	int error;
	u8 ctrl;
	struct sdio_func *func = (struct sdio_func *)host->priv_data;
	struct mmc_host *shost = func->card->host;

	if (!(func->card->host->caps & MMC_CAP_4_BIT_DATA))
		return 0;

	if (func->card->cccr.low_speed && !func->card->cccr.wide_bus)
		return 0;

	ctrl = sdio_f0_readb(func, SDIO_CCCR_IF, &error);
	if (error)
		return error;

	if (!(ctrl & SDIO_BUS_WIDTH_4BIT))
		return 0;

	ctrl &= ~SDIO_BUS_WIDTH_4BIT;
	ctrl |= SDIO_BUS_ASYNC_INT;

	sdio_f0_writeb(func, ctrl, SDIO_CCCR_IF, &error);
	if (error)
		return error;

	shost->ios.bus_width = MMC_BUS_WIDTH_1;
	shost->ops->set_ios(shost, &shost->ios);

	return 0;
}

static int sdio_device_init(struct host_io_info *host)
{
	int error;
	unsigned char value;
	struct sdio_func *func = (struct sdio_func *)host->priv_data;


	sdio_claim_host(func);

	/* Interrupt Enable for Function x */
	sdio_f0_writeb(func, 0x07, 0x04, &error);
	if (error)
		goto fail;

	/* 0x80: signal on the host-wake GPIO, 0x00: in-band on DAT1 */
	sdio_f0_writeb(func, host->irq > 0 ? 0x80 : 0x00, 0x16, &error);
	if (error)
		goto fail;

	/* Block Size for Function 0 */
	error = sdio_set_block_size(func, 512);
	if (error)
		goto fail;

	/* It can generate an interrupt to host */
	sdio_writeb(func, 0x02, 34, &error);
	if (error)
		goto fail;

	sdio_writeb(func, 0x02, 33, &error);
	if (error)
		goto fail;

	/* clear interrupt to host */
	value = sdio_readb(func, 32, &error);
	if (error)
		goto fail;

	sdio_writeb(func, value, 32, &error);
	if (error)
		goto fail;

	sdio_release_host(func);
#if SUPPORT_SDIO_SLEEP
	lpw_is_ready = 1;
#endif
	return 0;

fail:
	sdio_release_host(func);
	return error;
}

static int sdio_writeb_comp(struct host_io_info *host)
{
	int val;
	int onetime = 1; // us
	int count = 500*1000; // wait total (onetime*count) ms

	while (count--) {
		if (host->hal->fw_error_processing)
			return -1;

		val = _sdio_readb(host, SDIO_HOST_WRITE_REQ_INT_STA);
		if (val == 0) {
			return 0; // success
		} else if (val < 0) {
			rk915_err("%s: error %d\n", __func__, val);
			return -1;
		}
		udelay(onetime);
		//rk915_dbg(RK915_DBG_SDIO, "count = %d, val = %d\n", count, val);
	}

	rk915_err("%s: timeout val = %d\n", __func__, val);
	return -1; // wait timeout failed
}

static int sdio_notify_fw_pm(struct host_io_info *host, int wakeup)
{
#if NOTIFY_M0_SLEEP
	int msg = wakeup?IO_NOTIFY_WAKEUP:IO_NOTIFY_SLEEP;

	rk915_dbg(RK915_DBG_SDIO, "notify m0 %s\n", wakeup?"wakeup":"sleep");
	_sdio_writeb(host, IO_NOTIFY_ADDR, msg);
	return sdio_writeb_comp(host);
#else
	return 0;
#endif
}

static struct host_io_ops sdio_host_ops = {
	.io_init			= sdio_device_init,
	.io_send			= sdio_send_data,
	.io_send_sg			= sdio_send_data_sg,
	.io_recv			= sdio_recv_data,
	.lock				= sdio_lock,
	.unlock				= sdio_unlock,
	.io_readb			= _sdio_readb,
	.io_writeb			= _sdio_writeb,
	.io_writeb_comp		= sdio_writeb_comp,
	.io_ejtag			= sdio_disable_wide,
	.io_reset			= _sdio_reset,
	.io_notify_pm		= sdio_notify_fw_pm,
#if SUPPORT_SDIO_SLEEP
	.sleep				= sdio_sleep,
	.wakeup				= sdio_wakeup,
#endif
};

static void rk915_sdio_irq_handler(struct sdio_func *func)
{
	struct hal_priv *priv = sdio_get_drvdata(func);

	if (priv)
		hal_irq_handler(priv);
}

static int rk915_attach(struct sdio_func *func)
{
	struct hal_priv *priv;
	struct host_io_info *host;
	int ret;

	priv = rk915_core_init();
	if (!priv) {
		rk915_err("%s: rk915_core_init failed\n", __func__);
		return -ENOMEM;
	}
	sdio_set_drvdata(func, priv);

	host = priv->io_info;
	host->priv_data = (void *)func;
	host->dev = &func->dev;
	host->io_ops = &sdio_host_ops;
	/* Prefer the out-of-band host-wake line; boards that do not wire
	 * one fall back to the in-band DAT1 interrupt.
	 */
	host->irq = of_irq_get_byname(host->dev->of_node, "host-wake");
	if (host->irq > 0)
		rk915_dbg(RK915_DBG_SDIO, "%s: host-wake irq %d\n", __func__, host->irq);
	else
		rk915_dbg(RK915_DBG_SDIO, "%s: no host-wake irq, using in-band\n",
				__func__);

	host->bus_init = true;

	ret = rk915_device_probe(priv);
	if (ret) {
		host->bus_init = false;
		/* quiesce the in-band irq and unpublish the context before
		 * freeing it: the handler reads drvdata from its own thread
		 */
		rk915_sdio_release_irq(host);
		sdio_set_drvdata(func, NULL);
		rk915_core_deinit(priv);
		return ret;
	}

	return 0;
}

/* drop the claim that holds the bus clock up while fw is down */
void rk915_sdio_clock_release(struct host_io_info *host)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;

	if (!host->clk_claimed)
		return;

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_release_host(func);
	host->clk_claimed = false;
}

void rk915_sdio_release_irq(struct host_io_info *host)
{
	rk915_sdio_clock_release(host);
}

static int sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
	int ret = 0;

	rk915_dbg(RK915_DBG_SDIO, "sdio_func_num: 0x%X, vendor id: 0x%X, dev id: 0x%X, block size: 0x%X/0x%X\n",
			func->num, func->vendor, func->device, func->max_blksize, func->cur_blksize);

	/* All I/O goes through function 1. Don't bind function 2 so
	 * mmc_hw_reset() can reset the card in place instead of
	 * scheduling a remove/re-probe.
	 */
	if (func->num != 1)
		return -ENODEV;


	/* The CISTPL_FUNCE tuple advertises an enable timeout of 0 ms;
	 * use the SDIO 1.0 default instead of racing the function-ready
	 * poll.
	 */
	func->enable_timeout = 1000;

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	if (ret) {
		rk915_err("%s: failed to enable func, error %d\n", __func__, ret);
		sdio_release_host(func);
		return -1;
	}
	rk915_dbg(RK915_DBG_SDIO, "%s: enable func ok.\n", __func__);
	sdio_release_host(func);

	func->card->quirks |= MMC_QUIRK_LENIENT_FN0;
	sdio_reset = false;

	return rk915_attach(func);
}

static void sdio_remove(struct sdio_func *func)
{
	struct hal_priv *priv = sdio_get_drvdata(func);

	if (priv && priv->io_info->priv_data == (void *)func) {
		rk915_sdio_release_irq(priv->io_info);
		rk915_device_remove(priv);
		rk915_core_deinit(priv);
		sdio_set_drvdata(func, NULL);
	}
}

int rk915_sdio_power_cycle(struct host_io_info *host)
{
	struct sdio_func *func = (struct sdio_func *)host->priv_data;
	int ret;

	/* A power cycle re-probes the card, which needs the device lock
	 * the removal path holds: never start one while tearing down.
	 */
	if (host->hal->shutdown) {
		rk915_dbg(RK915_DBG_SDIO, "%s: skipped (shutdown)\n", __func__);
		return -ENODEV;
	}


	sdio_claim_host(func);
	ret = mmc_hw_reset(func->card);
	/* the CIS re-parse reset the bogus 0 ms enable timeout */
	func->enable_timeout = 1000;
	/* chip stalls if the bus clock gates on idle; claiming the sdio
	 * irq holds the clock up while fw is up, dropped at teardown.
	 * chip never signals in-band, host-wake GPIO drives rx.
	 */
	if (!ret && !host->clk_claimed &&
	    !sdio_claim_irq(func, rk915_sdio_irq_handler))
		host->clk_claimed = true;
	if (!ret)
		ret = sdio_enable_func(func);
	if (!ret)
		ret = sdio_set_block_size(func, 512);
	sdio_release_host(func);
	if (ret)
		rk915_err("%s: failed (%d)\n", __func__, ret);

	return ret;
}

#define dev_to_sdio_func(d)	container_of(d, struct sdio_func, dev)

#ifdef CONFIG_PM
static int sdio_suspend(struct device *dev)
{
	int ret = 0;
	mmc_pm_flag_t sdio_flags;
	struct sdio_func *func = dev_to_sdio_func(dev);

	struct hal_priv *priv = sdio_get_drvdata(func);

	if (!priv || (void *)func != priv->io_info->priv_data) {
		rk915_dbg(RK915_DBG_SDIO, "%s: is not rk915 sdio, skip it!\n", __func__);
		return 0;
	}


	sdio_flags = sdio_get_host_pm_caps(func);
	if (!(sdio_flags & MMC_PM_KEEP_POWER)) {
		dev_err(dev, "can't keep power while host is suspended\n");
		ret = -EINVAL;
		goto out;
	}

	/* keep power while host suspended */
	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (ret) {
		dev_err(dev, "error while trying to keep power\n");
		goto out;
	}

	if (priv->io_info->irq_request)
		enable_irq_wake(priv->io_info->irq);
	else if (sdio_flags & MMC_PM_WAKE_SDIO_IRQ)
		sdio_set_host_pm_flags(func, MMC_PM_WAKE_SDIO_IRQ);

	priv->during_pm_resume = 1;

#if SUPPORT_SDIO_SLEEP
	// change sdio clock to zero and iomux to gpio.
	sdio_sleep(priv->io_info);
#endif
	// disable interrupt
	// disable_irq(priv->io_info->irq);

out:
	return ret;
}

static int sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);

	struct hal_priv *priv = sdio_get_drvdata(func);

	if (!priv || (void *)func != priv->io_info->priv_data) {
		rk915_dbg(RK915_DBG_SDIO, "%s: is not rk915 sdio, skip it!\n", __func__);
		return 0;
	}


	if (priv->io_info->irq_request)
		disable_irq_wake(priv->io_info->irq);

	priv->during_pm_resume = 1;

#if SUPPORT_SDIO_SLEEP
	// change sdio clock to last clk
	sdio_wakeup(priv->io_info);
#endif

	return 0;
}

static const struct dev_pm_ops sdio_pm_ops = {
	.suspend = sdio_suspend,
	.resume  = sdio_resume,
};
#endif

static struct sdio_driver rk915_sdio_driver = {
		.name = "rk915_sdio",
		.id_table = rk915_sdio_devices,
		.probe = sdio_probe,
		.remove = sdio_remove,
#ifdef CONFIG_PM
		.drv = {
			.pm = &sdio_pm_ops,
		}
#endif
};

int rk915_sdio_register_driver(void)
{
	return sdio_register_driver(&rk915_sdio_driver);
}

void rk915_sdio_unregister_driver(void)
{
	sdio_unregister_driver(&rk915_sdio_driver);
}

int rk915_sdio_deinit(struct host_io_info *phost)
{
	phost->bus_init = false;

	return 0;
}

#ifdef ENABLE_FW_ERROR_RECOVERY
static int rk915_mmc_io_rw_direct_host(struct mmc_host *host, int write, unsigned int fn,
	unsigned int addr, u8 in, u8 *out)
{
	struct mmc_command cmd = {0};
	int err;

	/* sanity check */
	if (addr & ~0x1FFFF)
		return -EINVAL;

	cmd.opcode = SD_IO_RW_DIRECT;
	cmd.arg = write ? 0x80000000 : 0x00000000;
	cmd.arg |= fn << 28;
	cmd.arg |= (write && out) ? 0x08000000 : 0x00000000;
	cmd.arg |= addr << 9;
	cmd.arg |= in;
	cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(host, &cmd, 0);
	if (err)
		return err;

	{
		if (cmd.resp[0] & R5_ERROR)
			return -EIO;
		if (cmd.resp[0] & R5_FUNCTION_NUMBER)
			return -EINVAL;
		if (cmd.resp[0] & R5_OUT_OF_RANGE)
			return -ERANGE;
	}

	if (out)
		*out = cmd.resp[0] & 0xFF;

	return 0;
}

static int rk915_mmc_select_card(struct mmc_host *host, struct mmc_card *card)
{
	int err;
	struct mmc_command cmd = {0};

	cmd.opcode = MMC_SELECT_CARD;

	if (card) {
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	} else {
		cmd.arg = 0;
		cmd.flags = MMC_RSP_NONE | MMC_CMD_AC;
	}

	err = mmc_wait_for_cmd(host, &cmd, 3);
	if (err)
		return err;

	return 0;
}

static int rk915_sdio_reset(struct mmc_card *card)
{
	int ret;
	u8 abort;

	ret = rk915_mmc_io_rw_direct_host(card->host, 0, 0, SDIO_CCCR_ABORT, 0, &abort);
	if (ret)
		abort = 0x08;
	else
		abort |= 0x08;

	ret = rk915_mmc_io_rw_direct_host(card->host, 1, 0, SDIO_CCCR_ABORT, abort, NULL);
	return ret;
}

static int rk915_mmc_go_idle(struct mmc_host *host)
{
	int err;
	struct mmc_command cmd = {0};

	cmd.opcode = MMC_GO_IDLE_STATE;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_NONE | MMC_CMD_BC;

	err = mmc_wait_for_cmd(host, &cmd, 0);

	mdelay(1);

	host->use_spi_crc = 0;

	return err;
}

int rk915_mmc_send_io_op_cond(struct mmc_host *host, u32 ocr, u32 *rocr)
{
	struct mmc_command cmd = {0};
	int i, err = 0;

	cmd.opcode = SD_IO_SEND_OP_COND;
	cmd.arg = ocr;
	cmd.flags = MMC_RSP_SPI_R4 | MMC_RSP_R4 | MMC_CMD_BCR;

	for (i = 100; i; i--) {
		err = mmc_wait_for_cmd(host, &cmd, 3);
		if (err)
			break;

		/* if we're just probing, do a single pass */
		if (ocr == 0)
			break;

		/* otherwise wait until reset completes */
		{
			if (cmd.resp[0] & MMC_CARD_BUSY)
				break;
		}

		err = -ETIMEDOUT;

		mdelay(10);
	}

	return err;
}

int rk915_mmc_send_relative_addr(struct mmc_host *host, unsigned int *rca)
{
	int err;
	struct mmc_command cmd = {0};

	cmd.opcode = SD_SEND_RELATIVE_ADDR;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R6 | MMC_CMD_BCR;

	err = mmc_wait_for_cmd(host, &cmd, 3);
	if (err)
		return err;

	*rca = cmd.resp[0] >> 16;

	return 0;
}

static int rk915_mmc_sdio_switch_hs(struct mmc_card *card, int enable)
{
	int ret;
	u8 speed;

	if (!(card->host->caps & MMC_CAP_SD_HIGHSPEED))
		return 0;

	if (!card->cccr.high_speed)
		return 0;

	ret = rk915_mmc_io_rw_direct_host(card->host, 0, 0, SDIO_CCCR_SPEED, 0, &speed);
	if (ret)
		return ret;

	if (enable)
		speed |= SDIO_SPEED_EHS;
	else
		speed &= ~SDIO_SPEED_EHS;

	ret = rk915_mmc_io_rw_direct_host(card->host, 1, 0, SDIO_CCCR_SPEED, speed, NULL);
	if (ret)
		return ret;

	return 1;
}

static int rk915_sdio_enable_hs(struct mmc_card *card)
{
	int ret;

	ret = rk915_mmc_sdio_switch_hs(card, true);

	return ret;
}

static int rk915_sdio_enable_wide(struct mmc_card *card)
{
	int ret;
	u8 ctrl;

	if (!(card->host->caps & MMC_CAP_4_BIT_DATA))
		return 0;

	if (card->cccr.low_speed && !card->cccr.wide_bus)
		return 0;

	ret = rk915_mmc_io_rw_direct_host(card->host, 0, 0, SDIO_CCCR_IF, 0, &ctrl);
	if (ret)
		return ret;

	/* set as 4-bit bus width */
	ctrl &= ~SDIO_BUS_WIDTH_MASK;
	ctrl |= SDIO_BUS_WIDTH_4BIT;

	ret = rk915_mmc_io_rw_direct_host(card->host, 1, 0, SDIO_CCCR_IF, ctrl, NULL);
	if (ret)
		return ret;

	return 1;
}

static int rk915_sdio_enable_4bit_bus(struct mmc_card *card)
{
	return rk915_sdio_enable_wide(card);
}

static void rk915_mmc_power_up(struct mmc_host *host, u32 ocr)
{

	host->ios.chip_select = MMC_CS_DONTCARE;
	host->ios.bus_mode = MMC_BUSMODE_PUSHPULL;
	host->ios.power_mode = MMC_POWER_UP;
	host->ios.bus_width = MMC_BUS_WIDTH_1;
	host->ios.timing = MMC_TIMING_LEGACY;
	host->ops->set_ios(host, &host->ios);

	mdelay(10);

	host->ios.clock = host->f_init;

	host->ios.power_mode = MMC_POWER_ON;
	host->ops->set_ios(host, &host->ios);

	mdelay(10);
}

static void rk915_mmc_set_clock(struct mmc_host *host, unsigned int hz)
{
	if (hz > host->f_max)
		hz = host->f_max;

	host->ios.clock = hz;
	host->ops->set_ios(host, &host->ios);
}

static void rk915_mmc_set_bus_width(struct mmc_host *host, unsigned int width)
{
	host->ios.bus_width = width;
	host->ops->set_ios(host, &host->ios);
}


void rk915_sdio_set_clock(struct host_io_info *phost, int hz)
{
	struct sdio_func *func = (struct sdio_func *)phost->priv_data;
	struct mmc_host *shost = func->card->host;

	sdio_claim_host(func);
	rk915_mmc_set_clock(shost, hz);
	sdio_release_host(func);
}

int rk915_sdio_recovery_init(struct host_io_info *phost)
{
	struct sdio_func *func = (struct sdio_func *)phost->priv_data;
	struct mmc_host *shost = func->card->host;
	int err;
	u32 rocr, ocr, rca;


	sdio_claim_host(func);

	shost->ios.power_mode = MMC_POWER_OFF;
	rk915_mmc_power_up(shost, 1);

	err = rk915_sdio_reset(func->card);

	err = rk915_mmc_go_idle(shost);
	if (err) {
		rk915_err("rk915_mmc_go_idle failed (%d)\n", err);
		goto err_out;
	}

	ocr = 0;
	err = rk915_mmc_send_io_op_cond(shost, ocr, &rocr);
	if (err) {
		rk915_err("rk915_mmc_send_io_op_cond1 failed (%d)\n", err);
		goto err_out;
	}

	ocr = 0x1800000;
	err = rk915_mmc_send_io_op_cond(shost, ocr, &rocr);
	if (err) {
		rk915_err("rk915_mmc_send_io_op_cond2 failed (%d)\n", err);
		goto err_out;
	}

	err = rk915_mmc_send_relative_addr(shost, &rca);
	if (err) {
		rk915_err("rk915_mmc_send_relative_addr failed (%d)\n", err);
		goto err_out;
	}

	err = rk915_mmc_select_card(shost, func->card);
	if (err) {
		rk915_err("rk915_mmc_select_card failed (%d)\n", err);
		goto err_out;
	}

	err = rk915_sdio_enable_hs(func->card);
	if (err <= 0) {
		rk915_err("rk915_sdio_enable_hs failed (%d)\n", err);
		goto err_out;
	}

	rk915_mmc_set_clock(shost, 50000000);

	err = rk915_sdio_enable_4bit_bus(func->card);
	if (err > 0) {
		rk915_mmc_set_bus_width(shost, MMC_BUS_WIDTH_4);
	} else {
		rk915_err("rk915_sdio_enable_4bit_bus failed (%d)\n", err);
		goto err_out;
	}

	err = sdio_set_block_size(func, 512);
	if (err) {
		rk915_err("sdio_set_block_size failed (%d)\n", err);
		goto err_out;
	}

	func->enable_timeout = 1000;
	err = sdio_enable_func(func);
	if (err) {
		rk915_err("sdio_enable_func failed (%d)\n", err);
		goto err_out;
	}

	sdio_reset = false;

err_out:
	sdio_release_host(func);
	return err;
}
#else
int rk915_sdio_recovery_init(struct host_io_info *phost)
{
	return 0;
}
#endif
