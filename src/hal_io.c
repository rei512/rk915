// SPDX-License-Identifier: GPL-2.0-only
#include <linux/delay.h>
#include <linux/types.h>

#include "core.h"
#include "if_io.h"
#include "sdio.h"
#include "utils.h"

int num_streams_vpd;
unsigned char *rf_params_vpd;

static int rk915_unlock_data_read(struct hal_priv *priv, unsigned int
									addr, unsigned char *buf, unsigned int len)
{
	int retry = 3;
	int ret;

	do {
		ret = priv->io_info->io_ops->io_recv(priv->io_info, addr,
							buf, len);
		if (ret != -ETIMEDOUT)
			break;
		usleep_range(5000, 6000);
	} while (--retry > 0);

	return ret;
}

void rk915_lock(struct hal_priv *priv)
{
	priv->io_info->io_ops->lock(priv->io_info);
}

void rk915_unlock(struct hal_priv *priv)
{
	priv->io_info->io_ops->unlock(priv->io_info);
}

int rk915_io_reset(struct hal_priv *priv)
{
	if (priv->during_fw_download)
		return 0;
	priv->io_info->io_ops->io_reset(priv->io_info);
	return 0;
}

void rk915_notify_pm(struct hal_priv *priv, int wakeup)
{
	if (priv->io_info->io_ops->io_notify_pm) {
		if (!wakeup && rk915_wait_fw_ready_to_sleep(priv)) {
			rk915_lock(priv);
			priv->io_info->io_ops->io_notify_pm(priv->io_info, wakeup);
			rk915_unlock(priv);
		}
		rk915_lock(priv);
		priv->io_info->io_ops->io_notify_pm(priv->io_info, wakeup);
		rk915_unlock(priv);
	}
}

int rk915_writeb(struct hal_priv *priv, unsigned int addr, int val)
{
	int ret;

	rk915_lock(priv);
	ret = priv->io_info->io_ops->io_writeb(priv->io_info, addr, val);
	priv->io_info->io_ops->io_writeb_comp(priv->io_info);
	if (ret < 0)
		rk915_io_reset(priv);
	rk915_unlock(priv);

	if (ret < 0) {
		rk915_err("%s: addr=%d %d\n", __func__, addr, ret);
		rk915_signal_io_error(priv, FW_ERR_SDIO);
	}

	return ret;
}

int rk915_readb(struct hal_priv *priv, unsigned int addr)
{
	int val;

	rk915_lock(priv);
	val = priv->io_info->io_ops->io_readb(priv->io_info, addr);
	if (val < 0)
		rk915_io_reset(priv);
	rk915_unlock(priv);

	if (val < 0) {
		rk915_err("%s: addr=%d %d\n", __func__, addr, val);
		rk915_signal_io_error(priv, FW_ERR_SDIO);
	}

	return val;
}

int rk915_data_read(struct hal_priv *priv, unsigned int addr,
							unsigned char *buf, unsigned int len)
{
	int ret;

	rk915_lock(priv);
	ret =  priv->io_info->io_ops->io_recv(priv->io_info, addr, buf, len);
	if (ret < 0)
		rk915_io_reset(priv);
	rk915_unlock(priv);

	if (ret < 0) {
		rk915_err("%s: addr=%d len=%d %d\n", __func__, addr, len, ret);
		rk915_signal_io_error(priv, FW_ERR_SDIO);
	}

	return ret;
}

int rk915_data_write(struct hal_priv *priv, unsigned int addr,
							void *buf, size_t buf_len)
{
	int ret;
	/* do NOT pad to the sdio block size: this chip takes transfer
	 * length as message length, so pad bytes corrupt the frame and
	 * the interface stops coming up (measured). the byte-mode
	 * remainder CMD53 is unavoidable here.
	 */
	u32 len = ALIGN(buf_len, 4);

	rk915_lock(priv);
	ret = priv->io_info->io_ops->io_send(priv->io_info, addr, buf, len);
	if (ret < 0)
		rk915_io_reset(priv);
	rk915_unlock(priv);

	if (ret < 0) {
		rk915_err("%s: addr=%d len=%d %d\n",
						__func__, addr, (int)buf_len, ret);
		rk915_signal_io_error(priv, FW_ERR_SDIO);
	}

	return ret;
}

int rk915_data_write_sg(struct hal_priv *priv, unsigned int addr,
							void *buf, size_t buf_len)
{
	int ret;

	rk915_lock(priv);
	ret = priv->io_info->io_ops->io_send_sg(priv->io_info, addr, buf, buf_len);
	rk915_unlock(priv);

	if (ret < 0)
		rk915_io_reset(priv);

	return ret;
}

int rk915_ejtag(struct hal_priv *priv)
{
	int ret;

	rk915_lock(priv);
	ret = priv->io_info->io_ops->io_ejtag(priv->io_info);
	rk915_unlock(priv);

	return ret;
}

int rk915_io_init(struct hal_priv *priv)
{
	return priv->io_info->io_ops->io_init(priv->io_info);
}

// need lock before call
int rk915_read_data_len(struct hal_priv *priv)
{
	unsigned char len[2];
	unsigned int length;

	len[0] = priv->io_info->io_ops->io_readb(priv->io_info, IO_RECV_LEN_L);
	len[1] = priv->io_info->io_ops->io_readb(priv->io_info, IO_RECV_LEN_H);
	length = (len[1] << 8) | len[0];

	return length;
}

// need lock before call
static int rk915_notify(struct hal_priv *priv)
{
	int ret;

	ret = priv->io_info->io_ops->io_writeb(priv->io_info, IO_NOTIFY_ADDR, IO_NOTIFY_VAL);
	priv->io_info->io_ops->io_writeb_comp(priv->io_info);
	return ret;
}

// need lock before call
int rk915_clear_irq(struct hal_priv *priv)
{
	int ret;

	ret = priv->io_info->io_ops->io_writeb(priv->io_info, IO_INT_ADDR, IO_INT_CLR_IRQ_VAL);
	return ret;
}

int rk915_serias_read(struct hal_priv *priv, u16 addr,
						void *buf, size_t buf_len, u32 max_len)
{
	struct host_io_info *host = (struct host_io_info *)priv->io_info;
	int length;
	struct host_rpu_msg_hdr *mac_hdr;
	int ret;

	if (WARN_ON_ONCE(((unsigned long)buf & 0x3) ||
			 !virt_addr_valid(buf)))
		return -EINVAL;

	if (host->rx_serias_count > 0) {
		host->rx_serias_buf_curr += host->rx_serias_len[host->rx_serias_idx];

		++host->rx_serias_idx;
		mac_hdr = (struct host_rpu_msg_hdr *)host->rx_serias_buf_curr;
		host->rx_next_len = mac_hdr->length >> 16;
		mac_hdr->length = mac_hdr->length & 0x0000FFFF;
		length = mac_hdr->length + mac_hdr->payload_length;

		rk915_dbg(RK915_DBG_HALIO, "idx(%d) data len(%d) next_pkt(%d)\n",
						host->rx_serias_idx, length, host->rx_next_len);

		if ((host->rx_serias_idx + 1) >= host->rx_serias_count) {
			host->rx_serias_idx = -1;
			host->rx_serias_count = 0;
			if (host->rx_next_len == 0) {
				rk915_dbg(RK915_DBG_SDIO, "End of the continuous rx 1\n");
				rk915_lock(priv);
				if (rk915_clear_irq(priv)) {
					rk915_err("rk915_clear_irq error\n");
					length = -1;
					goto recv_exit;
				}
				/* notify m0 */
				if (rk915_notify(priv)) {
					rk915_err("rk915_notify error\n");
					length = -1;
					goto recv_exit;
				}
				rk915_unlock(priv);
			}
		}

		return length;
	}

	/* fw wedges the sdio engine if hit mid radio-retune; the
	 * chan-prog-done irq fires early, so wait out the retune.
	 * deadline guards a lost completion.
	 */
	if (priv->wifi && priv->wifi->hw) {
		struct img_priv *ipriv = priv->wifi->hw->priv;

		if (ipriv && !ipriv->chan_prog_done &&
		    time_before(jiffies, ipriv->chan_prog_deadline))
			msleep(50);
	}

	rk915_lock(priv);

	if (host->rx_next_len == 0) {
		length = rk915_read_data_len(priv);
		//rk915_dbg(RK915_DBG_SDIO, "rx len1 %x\n", length);
	} else {
		length = host->rx_next_len;
		//rk915_dbg(RK915_DBG_SDIO, "rx len2 %x\n", length);
	}

	if ((length > max_len) || (length < 0)) {
		rk915_err("%s: length(%d) too long error.\n", __func__, length);
		length = 0;
		goto recv_exit;
	}

	if (length == 0)
		goto recv_exit;

	ret = rk915_unlock_data_read(priv, 0, buf, length);
	if (ret) {
		if (net_ratelimit())
			rk915_err("%s: error %d len %x.\n", __func__, ret, length);
		length = 0;
		goto recv_exit;
	}

	host->rx_serias_buf_curr = buf;
	mac_hdr = (struct host_rpu_msg_hdr *)buf;

	/* it's an serias of package */
	if (mac_hdr->id == RPU_EVENT_RX_SERIAS &&
		*((u32 *)mac_hdr->hal_data.hal_data) == 0x3412ccff) {
		int i = 0;
		struct cmd_rx_ctrl *prx = (struct cmd_rx_ctrl *)buf;

		if (!CHECK_RX_PKT_CNT(prx->pkt_count)) {
			rk915_err("bad rx pkt_count %u\n", prx->pkt_count);
			length = 0;
			goto recv_exit;
		}

		for (i = 0; i < prx->pkt_count; i++)
			host->rx_serias_len[i] = prx->pkt_len[i];
		rk915_dbg(RK915_DBG_SDIO, "in rx serias: %d\n", prx->pkt_count);
		host->rx_serias_idx = 0;
		host->rx_serias_count = prx->pkt_count;
		/* skip the first pkt */
		host->rx_serias_buf_curr += round_up(sizeof(struct cmd_rx_ctrl), 4);
		mac_hdr = (struct host_rpu_msg_hdr *)host->rx_serias_buf_curr;
	}

	host->rx_next_len = mac_hdr->length >> 16;
	mac_hdr->length = mac_hdr->length & 0x0000FFFF;
	length = mac_hdr->length + mac_hdr->payload_length;

	rk915_dbg(RK915_DBG_SDIO, "idx(0) data len(%d) next_pkt(%d)\n",
					length, host->rx_next_len);

	if (host->rx_next_len == 0) {
		rk915_dbg(RK915_DBG_SDIO, "End of the continuous rx 2\n");
		if (rk915_clear_irq(priv))	{
			rk915_err("rk915_clear_irq error\n");
			length = -1;
			goto recv_exit;
		}
		/* notify m0 */
		if (rk915_notify(priv)) {
			rk915_err("rk915_notify error\n");
			length = -1;
			goto recv_exit;
		}
	}

recv_exit:
	rk915_unlock(priv);
//recv_exit_unlock:

	if (length <= 0) {
		struct img_priv *imgpriv =
			(priv->wifi && priv->wifi->hw) ? priv->wifi->hw->priv : NULL;
		/* sometimes wifi irq is triggered just after cmd_reset enable send to m0, but no data to read
		 * just skip it here, do not trigger fw error recovery
		 */
		if (length == 0 && (!imgpriv || !imgpriv->reset_complete))
			return length;
		rk915_io_reset(priv);
		rk915_signal_io_error(priv, FW_ERR_SDIO);
	}

	return length;
}
