#ifndef XPDS_SOFT_NET_H
#define XPDS_SOFT_NET_H 1

/*
 * 2.4 softnet api macros taken from rrunner.c
 */
#if (LINUX_VERSION_CODE < 0x02030e)
#define net_device device
#endif

#if (LINUX_VERSION_CODE >= 0x02031b)
#define NEW_NETINIT 1
#endif

#define HAS_SOFT_NET	0x02032b

#if (LINUX_VERSION_CODE < HAS_SOFT_NET)
/*
 * SoftNet changes
 */
#define dev_kfree_skb_irq(a)    dev_kfree_skb(a)
#define netif_wake_queue(dev)   clear_bit(0, &dev->tbusy)
#define netif_stop_queue(dev)   set_bit(0, &dev->tbusy)
#define net_device_stats	enet_statistics

static inline void netif_start_queue(struct net_device *dev)
{
        dev->tbusy = 0;
        dev->start = 1;
}

#define xpds_mark_net_bh(foo) mark_bh(foo)
#define xpds_if_busy(dev)     dev->tbusy
#define xpds_if_running(dev)  dev->start /* Currently unused. */
#define xpds_if_down(dev)	    {do{dev->start = 0;}while (0);}
#else
#define NET_BH              0
#define xpds_mark_net_bh(foo) {do{} while(0);}
#define xpds_if_busy(dev)     netif_queue_stopped(dev)
#define xpds_if_running(dev)  netif_running(dev)
#define xpds_if_down(dev)     {do{} while(0);}
#endif

#endif
