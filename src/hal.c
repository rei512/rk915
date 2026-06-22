// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/unaligned.h>

#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/debugfs.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/suspend.h>


#include "core.h"
#include "hal.h"
#include "hal_common.h"
#include "utils.h"
#include "wow.h"
#include "hal_io.h"
#include "if_io.h"
#include "platform.h"

#include <uapi/linux/sched/types.h>

#define ENABLE_RX_WORKQ		1
#define COMMAND_START_MAGIC 0xDEAD

//static int is_mem_bounce(void *virt_addr, int len);

const char *hal_name = "RPU_WIFI_HAL";

static unsigned int hal_cmd_sent;
static unsigned int hal_cmd_tx_send;
static unsigned int hal_event_recv;
static unsigned int hal_event_tx_done;
static unsigned int hal_event_cmd_proc_done;
static unsigned int hal_event_rx_recv;
static unsigned int hal_event_interrupts;
static unsigned int hal_event_rx_counts_one_interrupts[8];
static unsigned int hal_event_rx_counts_one_packet[8];
//static struct timer_list stats_timer;
static unsigned int alloc_skb_failures;

//static unsigned int rpu_ddr_base;


/* for send and receive count. */
static unsigned long tx_cnt;
static unsigned long rx_cnt;
/*RPU_DEBUG_HAL */

bool block_rpu_comm;

unsigned char vif_macs[2][ETH_ALEN];



/* Range check */
#define CHECK_EVENT_ADDR_RPU(x) ((x) >= HAL_RPU_GRAM_BASE && (x) <=\
				  (HAL_RPU_GRAM_BASE + \
				  priv->rpu_pkd_gram_len))

#define CHECK_EVENT_STATUS_ADDR_RPU(x) ((x) >= HAL_RPU_GRAM_BASE && (x) <=\
					 (HAL_RPU_GRAM_BASE + \
					 priv->rpu_pkd_gram_len))

#define CHECK_EVENT_LEN(x) ((x) < 0x5000)
/* #define CHECK_SRC_PTR(x, y) ((x) >= (y) && (x) <= (y) +
 * HAL_HOST_BOUNCE_BUF_LEN)
 */
#define CHECK_PKT_DESC(x) ((x) < (priv->rx_bufs_2k + priv->rx_bufs_12k))
/* MAX_RX_BUFS */




static int hal_reset_hal_params(struct hal_priv *priv)
{
	priv->cmd_cnt = COMMAND_START_MAGIC;
	priv->event_cnt = 0;
	return 0;
}



static void fw_err_work_fn(struct work_struct *work)
{
	struct hal_priv *priv = container_of(work, struct hal_priv, fw_err_work);
	struct wifi_dev *wdev;
	struct img_priv *imgpriv;
	bool was_started;

	if (priv->shutdown)
		goto out;

	wdev = priv->wifi;
	if (!wdev)
		goto out;
	imgpriv = wdev->hw ? wdev->hw->priv : NULL;
	was_started = imgpriv && imgpriv->state == STARTED;

	/* drop half-received rx state and drain the queues */
	priv->io_info->rx_serias_count = 0;
	priv->io_info->rx_next_len = 0;
#if ENABLE_RX_WORKQ
	wait_for_rxq_empty(priv);
#endif
	rpu_tx_proc_unfi_tx_done(imgpriv);
	rpu_if_free_outstnding(priv);

	/* no bus traffic until start() re-enables the hal */
	priv->hal_disabled = 1;
	skb_queue_purge(&priv->txq);

	/* fw only loads after a power cycle; the restart skips stop(),
	 * so wind device state back by hand
	 */
	wdev->params.fw_loaded = 0;
	if (imgpriv) {
#ifdef ENABLE_DAPT
		dapt_param_deinit(imgpriv);
#endif
		imgpriv->state = STOPPED;
	}

	rk915_err("fw error (%d): requesting mac80211 restart\n",
			   priv->fw_error_reason);

out:
	priv->fw_error_processing = 0;
	priv->fw_error = 0;
	rk915_wake_waiters(priv);

	if (!priv->shutdown && was_started)
		ieee80211_restart_hw(wdev->hw);

	__pm_relax(priv->fw_err_ws);
}

static int tx_thread(void *data)
{
	struct hal_priv *priv = data;
	struct sk_buff *skb;
	int ret;
	struct img_priv *imgpriv;
	struct host_rpu_msg_hdr *phdr;
#ifdef DUMP_MORE_DEBUG_INFO
	char cmd_str[64];
#endif

	sched_set_fifo_low(current);

	while (1) {
		wait_event_interruptible(priv->tx_worker.waitq,
					 atomic_read(&priv->tx_worker.pending) ||
					 kthread_should_stop());

		if (kthread_should_stop())
			break;

		atomic_set(&priv->tx_worker.pending, 0);

		if (priv->fw_error_processing || !priv->hal_init)
			continue;
	while (1) {
		if (skb_peek(&priv->txq) == NULL)
			break;

		if (block_rpu_comm) {
			rk915_dbg(RK915_DBG_TX, "%s: break with block_rpu_comm\n", __func__);
			break;
		}

		skb = skb_dequeue(&priv->txq);
		if (skb == NULL)
			break;

		tx_cnt++;
		rk915_dbg(RK915_DBG_HAL, "%s: tx_cnt=%ld cmd_cnt=0x%X event_cnt=0x%X\n",
				hal_name,
				tx_cnt,
				priv->cmd_cnt,
				priv->event_cnt);
		if (DUMP_HAL) {
			rk915_dbg(RK915_DBG_HAL, "%s: xmit dump\n", hal_name);
			rk915_dbg_dump(RK915_DBG_DUMP_HAL, " ", DUMP_PREFIX_NONE, 16, 4,
					 skb->data, 32, 1);
		}


		/* workaround m0 died problem during resume */
		phdr = (struct host_rpu_msg_hdr *)skb->data;
		if (phdr->id != RPU_CMD_RESET && phdr->id != RPU_CMD_PS_ECON_CFG) {
			imgpriv = (priv->wifi && priv->wifi->hw) ?
					priv->wifi->hw->priv : NULL;
			wait_for_pm_resume_done(priv);
	}

#ifdef DUMP_MORE_DEBUG_INFO
		convert_cmd_to_str(phdr->id, cmd_str, sizeof(cmd_str));
		rk915_dbg(RK915_DBG_HAL, "send %s(%d)\n", cmd_str, phdr->id);
#endif


		if (priv->hal_disabled) {
			dev_kfree_skb_any(skb);
			break;
		}

		/* a command and its data must not be interleaved with rx */
		mutex_lock(&priv->txrx_mutex);

		/* one host claim for cmd+frames; claims nest per task */
		rk915_lock(priv);

		ret = rk915_data_write(priv, 0, skb->data, skb->len);
		if (ret)
			rk915_err("%s: ret = %d, pkt_len = %d.\n",
				      __func__, ret, skb->len);

		if (rpu_is_cmd_has_data(skb->data)) {
			hal_cmd_tx_send++;
			rpu_send_cmd_datas(skb->data, priv);
		}

		rk915_unlock(priv);
		mutex_unlock(&priv->txrx_mutex);

		priv->cmd_cnt++;
		hal_cmd_sent++;

		dev_kfree_skb_any(skb);
	}
	}

	return 0;
}

#if ENABLE_RX_WORKQ
static void hal_rx_queue_work(struct hal_priv  *priv)
{
	queue_work(priv->rx_wkq, &priv->rx_work);
}
#endif

static void rk915_worker_kick(struct rk915_worker *worker)
{
	atomic_set(&worker->pending, 1);
	wake_up(&worker->waitq);
}

static int rk915_worker_start(struct rk915_worker *worker,
				int (*fn)(void *data), void *data,
				const char *name)
{
	init_waitqueue_head(&worker->waitq);
	atomic_set(&worker->pending, 0);
	worker->task = kthread_run(fn, data, "%s", name);
	if (IS_ERR(worker->task)) {
		int err = PTR_ERR(worker->task);

		worker->task = NULL;
		return err;
	}

	return 0;
}

static void rk915_worker_stop(struct rk915_worker *worker)
{
	if (worker->task) {
		kthread_stop(worker->task);
		worker->task = NULL;
	}
}

static void hal_tx_queue_work(struct hal_priv *priv)
{
	if (priv->tx_worker.task && !priv->hal_disabled)
		rk915_worker_kick(&priv->tx_worker);
}

static void hal_rx_thread_trigger(struct hal_priv  *priv)
{
	if (priv->rx_worker.task && !priv->hal_disabled) {
		rk915_worker_kick(&priv->rx_worker);
		hal_event_interrupts++;
	}
}

static void _hal_send(struct hal_priv  *priv,
			  struct sk_buff   *skb)
{
	skb_queue_tail(&priv->txq, skb);
	//tasklet_schedule(&priv->tx_tasklet);
	hal_tx_queue_work(priv);
}

static void hal_send(struct hal_priv *priv, void *msg,
			void *payload,
			unsigned int descriptor_id)
{

	_hal_send(priv, msg);

}

#if ENABLE_RX_WORKQ
static void hal_recv(struct hal_priv *priv, struct sk_buff *skb)
{
#define MAX_RX_QUEUE	8192
	if (skb_queue_len(&priv->rxq) > MAX_RX_QUEUE) {
		if (net_ratelimit()) {
			struct host_rpu_msg_hdr *hdr = (struct host_rpu_msg_hdr *)skb->data;

			rk915_err("%s: rx queue large than %d, drop it(%p)(%d)!\n",
					__func__, MAX_RX_QUEUE, skb, hdr->id);
		}
		dev_kfree_skb_any(skb);
		return;
	}
	rk915_dbg(RK915_DBG_HAL, "%s: rx enqueue %p\n", __func__, skb);
	skb_queue_tail(&priv->rxq, skb);
	if (priv->max_rxq_len < skb_queue_len(&priv->rxq))
		priv->max_rxq_len = skb_queue_len(&priv->rxq);
	hal_rx_queue_work(priv);
}
#endif

static void rx_counts_one_statistics(unsigned int count)
{
	// statistics how many rx data received in one interrupts
	if (count <= 2*1024)
		hal_event_rx_counts_one_interrupts[0]++;
	else if (count <= 4*1024)
		hal_event_rx_counts_one_interrupts[1]++;
	else if (count <= 6*1024)
		hal_event_rx_counts_one_interrupts[2]++;
	else if (count <= 8*1024)
		hal_event_rx_counts_one_interrupts[3]++;
	else if (count <= 10*1024)
		hal_event_rx_counts_one_interrupts[4]++;
	else if (count <= 12*1024)
		hal_event_rx_counts_one_interrupts[5]++;
	else if (count <= 14*1024)
		hal_event_rx_counts_one_interrupts[6]++;
	else
		hal_event_rx_counts_one_interrupts[7]++;
}

//static void rx_tasklet_fn(unsigned long data)
#if ENABLE_RX_WORKQ
static void rx_work_fn(struct work_struct *work)
{
	struct hal_priv *priv = container_of(work, struct hal_priv, rx_work);
	struct sk_buff *skb;


	while (1) {
		if (skb_peek(&priv->rxq) == NULL)
			break;

		skb = skb_dequeue(&priv->rxq);
		if (skb == NULL)
			break;

		rk915_dbg(RK915_DBG_HAL, "%s: rx dequeue %p\n", __func__, skb);

		priv->rcv_handler(skb);
	}
	rk915_wake_waiters(priv);
}
#endif

static int rx_thread(void *data)
{
	struct hal_priv *priv = data;
	unsigned char *nbuff;
	struct sk_buff *rx_skb;
	struct host_rpu_msg_hdr *hdr;
	int data_length = 0;
	unsigned int max_data_size = MAX_DATA_SIZE_2K;
	unsigned int payload_length, length;
	unsigned int event;
	unsigned int rx_counts_one = 0;
#ifdef DUMP_MORE_DEBUG_INFO
	char evt_str[64];
#endif


	memset(hal_event_rx_counts_one_interrupts, 0, 8*sizeof(unsigned int));
	memset(hal_event_rx_counts_one_packet, 0, 8*sizeof(unsigned int));

	sched_set_fifo_low(current);
	while (1) {
		if (priv->io_info->rx_serias_count == 0 &&
			priv->io_info->rx_next_len == 0) {
			if (rx_counts_one > 0) {
				rx_counts_one_statistics(rx_counts_one);
				rx_counts_one = 0;
			}
			rk915_irq_enable(priv, 1);
			wait_event_interruptible(priv->rx_worker.waitq,
						 atomic_read(&priv->rx_worker.pending) ||
						 kthread_should_stop());
			atomic_set(&priv->rx_worker.pending, 0);
		}

		if (kthread_should_stop())
			break;

		/* fw halted: nothing to read, wake line may be stuck
		 * asserted. leave the irq masked until next bring-up.
		 */
		if (block_rpu_comm) {
			priv->io_info->rx_serias_count = 0;
			priv->io_info->rx_next_len = 0;
			continue;
		}

		rx_cnt++;
		rk915_dbg(RK915_DBG_HAL, "%s:rx_cnt=%ld cmd_cnt=0x%X event_cnt=0x%X\n",
			 hal_name, rx_cnt, priv->cmd_cnt, priv->event_cnt);

//		mutex_lock(&priv->txrx_mutex);
		data_length = rk915_serias_read(priv, 0, priv->io_info->rx_serias_buf,
									  0, MAX_RX_SERIAS_BYTES);
//		mutex_unlock(&priv->txrx_mutex);
		if (data_length <= 0) {
			if (!block_rpu_comm && net_ratelimit())
				rk915_err("%s: error datalen: %x.\n", __func__, data_length);
			priv->io_info->rx_serias_count = 0;
			priv->io_info->rx_next_len = 0;
			continue;
		}
		rx_counts_one += data_length;

		rx_skb = alloc_skb(data_length, GFP_ATOMIC);
		if (rx_skb) {
			memcpy(skb_put(rx_skb, data_length),
				priv->io_info->rx_serias_buf_curr, data_length);
		} else {
			alloc_skb_failures++;
			continue;
		}

		nbuff = rx_skb->data;
		hdr = (struct host_rpu_msg_hdr *)nbuff;
		event = hdr->id & 0xffff;
		if (event == RPU_EVENT_RX) {
			hal_event_rx_recv++;
				/* 802.11hdr + payload Len*/
				payload_length = hdr->payload_length;
				length = hdr->length;
				/* Control Info Len*/
				data_length = payload_length + length;

				//rk915_dbg(RK915_DBG_HAL, "receive %d (%d)\n", event, data_length);
				/* Complete data length to be copied */
				rk915_dbg(RK915_DBG_HAL, "%s: Payload Len =%d(0x%x),\n",
					   hal_name,
					   payload_length,
					   payload_length);

				rk915_dbg(RK915_DBG_HAL, "Len=%d(0x%x),\n",
					   length,
					   length);

				rk915_dbg(RK915_DBG_HAL, "Data Len = %d(0x%x)\n",
					   data_length,
					   data_length);
				if (data_length > max_data_size) {
					rk915_err("Max length exceeded:\n");
					rk915_err(" payload_len: %d len:%d\n",
						payload_length,
						length);
					continue;
				}
		} else	{
			//rk915_dbg(RK915_DBG_HAL, "receive %d\n", event);
			/* MSG from LMAC, non-data*/
			if (event == RPU_EVENT_TX_DONE)
				hal_event_tx_done++;
			else if (event == RPU_EVENT_COMMAND_PROC_DONE)
				hal_event_cmd_proc_done++;
			hal_event_recv++;
		}

		if (priv->fw_error_processing) {
			rk915_dbg(RK915_DBG_RECOVERY, "event %d\n", event);
			priv->fw_error_cmd_done = 1;
			rk915_wake_waiters(priv);
			dev_kfree_skb_any(rx_skb);
			continue;
		}

		/*
		 * we should notify ieee80211_connection_loss after device resume finished
		 * else mac80211 will discard it
		 */
		if (event == RPU_EVENT_DISCONNECTED)
			wait_for_pm_resume_done(priv);

#ifdef DUMP_MORE_DEBUG_INFO
		convert_event_to_str(event, evt_str, sizeof(evt_str));
		rk915_dbg(RK915_DBG_HAL, "receive %s(%d)\n", evt_str, event);
#endif

#if ENABLE_RX_WORKQ
		hal_recv(priv, rx_skb);
#else
		priv->rcv_handler(rx_skb);
#endif
	}
	return 0;
}


static void hal_register_callback(struct hal_priv *priv, msg_handler handler)
{
	priv->rcv_handler = handler;
}

int hal_irq_handler(struct hal_priv *p)
{
	struct hal_priv *priv = p;

#ifdef CONFIG_PM
	rx_interrupt_status = 1;
#endif

	/* no rx bus access during fw download: corrupts the transfer */
	if (priv->during_fw_download)
		return 0;

	/* masked until the rx thread drains the event queue */
	rk915_irq_enable(priv, 0);

	hal_rx_thread_trigger(priv);

	priv->event_cnt++;

	return 0;
}


static void hal_enable_int(struct hal_priv *priv)
{
	rk915_irq_enable(priv, 1);
}


static void hal_disable_int(struct hal_priv *priv)
{
	rk915_irq_enable(priv, 0);
}



static ssize_t proc_write_hal_stats(struct file *file,
		const char __user    *buffer,
		size_t		     count,
		loff_t               *ppos)
{
	char buf[50];

	if (count >= sizeof(buf))
		count = sizeof(buf)-1;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	return count;
}

static int proc_read_hal_stats(struct seq_file *m, void *v)
{
	int i = 0;

	seq_printf(m, "Alloc SKB Failures: %d\n",
		   alloc_skb_failures);


	seq_printf(m, "hal_cmd_sent_cnt: %d\n",
		   hal_cmd_sent - hal_cmd_tx_send);
	seq_printf(m, "hal_event_cmd_proc_done_cnt: %d\n",
		   hal_event_cmd_proc_done);
	seq_printf(m, "hal_cmd_tx_sent_cnt: %d\n",
		   hal_cmd_tx_send);
	seq_printf(m, "hal_event_tx_done_cnt: %d\n",
		   hal_event_tx_done);
	seq_printf(m, "hal_event_recv_cnt: %d\n",
		   hal_event_recv);
	seq_printf(m, "hal_event_rx_recv_cnt: %d\n",
		   hal_event_rx_recv);

	seq_printf(m, "hal_event_interrupts: %d\n",
		   hal_event_interrupts);

	for (i = 0; i < 8; i++) {
		seq_printf(m, "rx_counts_one_interrupts_%dK: %d\n",
			   2*(i+1), hal_event_rx_counts_one_interrupts[i]);
	}

	for (i = 0; i < 8; i++) {
		seq_printf(m, "rx_count_one_packet_%dK: %d\n",
			   2*(i+1), hal_event_rx_counts_one_packet[i]);
	}

	return 0;
}


static int proc_open_hal_stats(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_hal_stats, NULL);
}

static const struct file_operations params_fops_hal_stats = {
	.open = proc_open_hal_stats,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = proc_write_hal_stats,
	.release = single_release
};

static int hal_proc_init(struct dentry *hal_proc_dir_entry)
{
	debugfs_create_file("hal_stats", 0444, hal_proc_dir_entry,
				NULL, &params_fops_hal_stats);

	return 0;
}




static int hal_start(struct hal_priv *priv)
{
	priv->hal_disabled = 0;


	/* Enable host_int and rpu_int */
	hal_enable_int(priv);

	return 0;
}


static int hal_stop(struct hal_priv *priv)
{
	priv->hal_disabled = 1;

	/* Disable host_int and rpu_irq */
	hal_disable_int(priv);
	return 0;
}

static int rk915_pm_notifier(struct notifier_block *nb, unsigned long action,
			void *data)
{
	struct hal_priv *priv = container_of(nb, struct hal_priv, pm_notifier);

	switch (action) {
	case PM_SUSPEND_PREPARE:
		break;
	case PM_POST_SUSPEND:
		priv->during_pm_resume = 0;
		rk915_wake_waiters(priv);
		break;
	case PM_POST_RESTORE:
	case PM_RESTORE_PREPARE:
	default:
		break;
	}

	return NOTIFY_DONE;
}

/* Unmap and release all resoruces*/
static int cleanup_all_resources(struct hal_priv *priv)
{
	unregister_pm_notifier(&priv->pm_notifier);

	return 0;
}
static int hal_deinit(struct hal_priv *priv)
{
	struct sk_buff *skb;

	if (!priv->hal_init) {
		proc_exit(priv->wifi);
		return 0;
	}

	/* stop recovery before the umac goes away: the work derefs the
	 * ieee80211_hw that _rpu_umac_if_exit frees
	 */
	cancel_work_sync(&priv->fw_err_work);

	_rpu_umac_if_exit(priv);

#if ENABLE_RX_WORKQ
	cancel_work_sync(&priv->rx_work);
	if (priv->rx_wkq != NULL) {
		destroy_workqueue(priv->rx_wkq);
		priv->rx_wkq = NULL;
	}
#endif

	wakeup_source_unregister(priv->fw_err_ws);
	priv->fw_err_ws = NULL;

	rk915_worker_stop(&priv->tx_worker);
	rk915_worker_stop(&priv->rx_worker);

	while ((skb = skb_dequeue(&priv->rxq)))
		dev_kfree_skb_any(skb);

	while ((skb = skb_dequeue(&priv->txq)))
		dev_kfree_skb_any(skb);

	cleanup_all_resources(priv);

	proc_exit(priv->wifi);

	priv->hal_init = 0;
	return 0;
}


static int hal_init(struct hal_priv *priv)
{
	int err = 0;

	mutex_init(&priv->txrx_mutex);

	priv->hal_disabled = 1;

	err = rk915_worker_start(&priv->tx_worker, tx_thread, priv,
				 "rk915_tx");
	if (err)
		goto tx_thread_fail;

	err = rk915_worker_start(&priv->rx_worker, rx_thread, priv,
				 "rk915_rx");
	if (err)
		goto rx_thread_fail;

#if ENABLE_RX_WORKQ
	priv->rx_wkq = create_singlethread_workqueue("rk915_rx_wkq");
	if (priv->rx_wkq == NULL) {
		rk915_err("%s: create rx wkq failed\n", hal_name);
		err = -ENOMEM;
		goto error1;
	}
	INIT_WORK(&priv->rx_work, rx_work_fn);
#endif
	skb_queue_head_init(&priv->rxq);
	skb_queue_head_init(&priv->txq);

	INIT_WORK(&priv->fw_err_work, fw_err_work_fn);

	priv->fw_err_ws = wakeup_source_register(NULL, "rk915_fw_err");

	priv->pm_notifier.notifier_call = rk915_pm_notifier;
	register_pm_notifier(&priv->pm_notifier);

	if (_rpu_umac_if_init(priv) < 0) {
		rk915_err("%s: wlan_init failed\n", hal_name);
		err = -ENOMEM;
		goto error2;
	}

	err = hal_proc_init(rk915_debugfs_dir(priv->wifi));
	if (err)
		goto umac_if_deinit;

	priv->cmd_cnt = COMMAND_START_MAGIC;
	priv->event_cnt = 0;
	priv->hal_init = 1;

	return err;
umac_if_deinit:
	_rpu_umac_if_exit(priv);
	proc_exit(priv->wifi);
error2:
	wakeup_source_unregister(priv->fw_err_ws);
	priv->fw_err_ws = NULL;
	if (priv->rx_wkq != NULL)
		destroy_workqueue(priv->rx_wkq);
	unregister_pm_notifier(&priv->pm_notifier);
error1:
	rk915_worker_stop(&priv->rx_worker);
rx_thread_fail:
	rk915_worker_stop(&priv->tx_worker);
tx_thread_fail:
	return err;
}

static void hal_deinit_bufs(struct hal_priv *priv)
{
	kfree(priv->tx_buf_info);
	priv->tx_buf_info = NULL;
	kfree(priv->rx_tmp_buf);
	priv->rx_tmp_buf = NULL;
}


static int hal_init_bufs(struct hal_priv *priv,
			 unsigned int tx_bufs,
			 unsigned int rx_bufs_2k,
			 unsigned int rx_bufs_12k,
			 unsigned int tx_max_data_size)
{
	priv->rx_tmp_buf = kzalloc(MAX_DATA_SIZE_2K, GFP_KERNEL);
	if (!priv->rx_tmp_buf)
		goto err;

	return 0;
err:

	hal_deinit_bufs(priv);

	return -1;
}



static int hal_unmap_tx_buf(struct hal_priv *priv, int pkt_desc, int frame_id)
{

	return 0;
}

void wow_enable_irq_wake(struct hal_priv *priv)
{
	if (priv->io_info->irq_request)
		enable_irq_wake(priv->io_info->irq);
}

void wow_disable_irq_wake(struct hal_priv *priv)
{
	if (priv->io_info->irq_request)
		disable_irq_wake(priv->io_info->irq);
}



struct hal_ops_tag hal_ops = {
	.init = hal_init,
	.deinit	= hal_deinit,
	.start = hal_start,
	.stop = hal_stop,
	.register_callback = hal_register_callback,
	.send = hal_send,
	.init_bufs = hal_init_bufs,
	.deinit_bufs = hal_deinit_bufs,
	.unmap_tx_buf = hal_unmap_tx_buf,
	.reset_hal_params	= hal_reset_hal_params,
	.get_dev = hal_get_dev,
#ifdef CONFIG_PM
	.enable_irq_wake = wow_enable_irq_wake,
	.disable_irq_wake = wow_disable_irq_wake,
#endif
};

