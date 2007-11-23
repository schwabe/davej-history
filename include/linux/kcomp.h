/*
 * Kernel compatibility glue to allow USB compile on 2.2.x kernels
 */

#include <linux/list.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <linux/pagemap.h>

#define __exit

#ifdef __alpha
extern long __kernel_thread (unsigned long, int (*)(void *), void *);
static inline long kernel_thread (int (*fn) (void *), void *arg, unsigned long flags)
{
	return __kernel_thread (flags | CLONE_VM, fn, arg);
}
#undef CONFIG_APM
#endif


#define pci_enable_device(x)			0

#define page_address(x)				(x | PAGE_OFFSET)

#ifdef MODULE
#define module_init(x)				int init_module(void) { return x(); }
#define module_exit(x)				void cleanup_module(void) { x(); }
#define THIS_MODULE				(&__this_module)
#else
#define module_init(x)				int x##_module(void) { return x(); }
#define module_exit(x)				void x##_module(void) { x(); }
#define THIS_MODULE				NULL
#endif

#define	TTY_DRIVER_NO_DEVFS			0

#define __initcall(x)

#define	net_device			device
#define dev_kfree_skb_irq(a)		dev_kfree_skb(a)
#define netif_wake_queue(dev)		clear_bit(0, &dev->tbusy)
#define netif_stop_queue(dev)		set_bit(0, &dev->tbusy)
#define netif_start_queue(dev)		do { dev->tbusy = 0; dev->interrupt = 0; dev->start = 1; } while (0)
#define netif_queue_stopped(dev)	dev->tbusy
#define netif_running(dev)		dev->start

#define NET_XMIT_SUCCESS	0
#define NET_XMIT_DROP		1
#define NET_XMIT_CN		2

#define IORESOURCE_IO			1
#define pci_resource_start(dev, i)	(dev->base_address[i] & ~IORESOURCE_IO)
#define pci_resource_flags(dev, i)	(dev->base_address[i] & IORESOURCE_IO)

