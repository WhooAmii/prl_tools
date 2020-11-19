/*
 * @file  compat.h
 * @author vgusev
 *
 * Copyright (C) 1999-2016 Parallels International GmbH.
 * All Rights Reserved.
 * http://www.parallels.com
 */

#ifndef __COMPAT_H__
#define __COMPAT_H__

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,40)) && \
    (LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0))
/* Fedora 15 uses 2.6.4x kernel version enumeration instead of 3.x */
#define MINOR_3X_LINUX_VERSION LINUX_VERSION_CODE - KERNEL_VERSION(2,6,40)
#define REAL_LINUX_VERSION_CODE	KERNEL_VERSION(3,MINOR_3X_LINUX_VERSION,0)
#else
#define REAL_LINUX_VERSION_CODE	LINUX_VERSION_CODE
#endif

#ifndef IRQF_SHARED
#define IRQF_SHARED SA_SHIRQ
#endif

#include <linux/netdevice.h>

/*
 * The HAVE_NET_DEVICE_OPS first appeared on 2.6.29
 * and was completely removed in 3.1.0. Define it themselves.
 */
#if !defined(HAVE_NET_DEVICE_OPS)
#  define HAVE_NET_DEVICE_OPS
#endif


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
#define SET_ETHTOOL_OPS(netdev, ops) ((netdev)->ethtool_ops = (ops))
#endif

#ifdef SET_ETHTOOL_OPS
#define HAVE_ETHTOOL_OPS   1
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)

#define compat_alloc_netdev(sizeof_priv, mask, net_device) \
	alloc_netdev((sizeof_priv), (mask), NET_NAME_ENUM, (net_device))

#else

#define compat_alloc_netdev(sizeof_priv, mask, net_device) \
	alloc_netdev((sizeof_priv), (mask), (net_device))

#endif /* 3.17 */

#endif	/* __COMPAT_H__ */
