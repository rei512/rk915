/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021, Fuzhou Rockchip Electronics Co., Ltd
 */

#ifndef _DESCRIPTOR_H_
#define _DESCRIPTOR_H_

#define NUM_ACS 5

/* Control Path: 1 for CMD and 1 for EVENT */
#define NUM_CTRL_DESCS	2

/* Data Path */

/* Reserved TX descriptors per AC
 * (must be multiple of 2, minimum of 2
 * and maximum of 4)
 */

#define NUM_TX_DESCS_PER_AC 2
/* Spare Descriptors shared between all ACs
 * (at least 1 and maximum of 2)
 */
#define NUM_SPARE_TX_DESCS 2

#define NUM_TX_DESCS ((NUM_ACS *  NUM_TX_DESCS_PER_AC) + NUM_SPARE_TX_DESCS)

/* Max no of sub frames in an AMPDU */
/* the tx command carries up to MAX_TX_CMDS subframe descriptors */
#define MAX_SUBFRAMES_IN_AMPDU_HT MAX_TX_CMDS
#define MAX_FW_TX_PKGS (4*MAX_SUBFRAMES_IN_AMPDU_HT)


#define NUM_RX_BUFS_2K	16
#define NUM_RX_BUFS_12K	16

#define RX_BUFS_12K_START 0
#define RX_BUFS_12K_END  (RX_BUFS_12K_START + NUM_RX_BUFS_12K)

#define RX_BUFS_2K_START NUM_RX_BUFS_12K
#define RX_BUFS_2K_END   (RX_BUFS_2K_START + NUM_RX_BUFS_2K)

#endif /* _DESCRIPTOR_H_ */
/* EOF */
