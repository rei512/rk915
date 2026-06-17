// SPDX-License-Identifier: GPL-2.0-only
#include <linux/irq.h>
#include <linux/rfkill.h>

#include "core.h"
#include "if_io.h"
#include "platform.h"


void rk915_rescan_card(unsigned int insert)
{
	//rockchip_wifi_set_carddetect(insert);
}

void rk915_poweron(void)
{
	//rockchip_wifi_power(0);
	//mdelay(RK915_POWER_ON_DELAY_MS);
	//rockchip_wifi_power(1);
}

void rk915_poweroff(void)
{
	//rockchip_wifi_power(0);
}

/* The host-wake line stays asserted until the rx thread drains the
 * chip's event queue, so the level irq must stay masked from handler
 * entry until the drain completes or it refires continuously.
 */
void rk915_irq_enable(struct hal_priv *priv, int enable)
{
	struct host_io_info *host = priv ? priv->io_info : NULL;

	if (!host || !host->irq_request)
		return;

	if (enable) {
		if (atomic_xchg(&host->irq_masked, 0))
			enable_irq(host->irq);
	} else {
		if (!atomic_xchg(&host->irq_masked, 1))
			disable_irq_nosync(host->irq);
	}
}

static irqreturn_t hal_interrupt(int irq, void *dev_id)
{
	hal_irq_handler(dev_id);
	return IRQ_HANDLED;
}

int rk915_register_irq(struct host_io_info *host)
{
	unsigned long flags;
	int ret;

	/* the in-band SDIO interrupt is claimed at probe (it also keeps
	 * the bus clock running); nothing more to do without a host-wake
	 * line
	 */
	if (host->irq <= 0)
		return 0;

	/* already requested by a previous bring-up */
	if (host->irq_request)
		return 0;

	/* trigger type comes from the DT interrupt specifier */
	flags = irq_get_trigger_type(host->irq);
	if (!(flags & IRQF_TRIGGER_MASK))
		flags = IRQF_TRIGGER_RISING;

	ret = devm_request_threaded_irq(host->dev, host->irq, NULL,
					hal_interrupt, flags | IRQF_ONESHOT,
					"rk915", host->hal);
	if (ret == 0)
		host->irq_request = true;

	return ret;
}

int rk915_free_irq(struct host_io_info *host)
{
	if (host->irq <= 0) {
		rk915_sdio_release_irq(host);
		return 0;
	}
	if (host->irq_request) {
		devm_free_irq(host->dev, host->irq, host->hal);
		host->irq_request = false;
	}

	return 0;
}

int rk915_bus_register_driver(void)
{
	return rk915_sdio_register_driver();
}

void rk915_bus_unregister_driver(void)
{
	rk915_sdio_unregister_driver();
}

int rk915_platform_bus_init(struct host_io_info *phost)
{
	/* The SDIO bus is wired up in sdio_probe() before we get here */
	if (!phost->bus_init) {
		rk915_err("%s: bus not initialized\n", __func__);
		return -ENODEV;
	}

	return 0;
}

int rk915_platform_bus_rec_init(struct host_io_info *phost)
{
	return rk915_sdio_recovery_init(phost);
}

int rk915_platform_bus_deinit(struct host_io_info *phost)
{
	if (phost->bus_init)
		return rk915_sdio_deinit(phost);
	else
		return 0;
}
