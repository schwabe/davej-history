/*
 * Kernel compatibility glue to allow USB compile on 2.2.x kernels
 */

#include <linux/list.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <linux/pagemap.h>

#define LIST_HEAD_INIT(name) { &(name), &(name) }
static __inline__ void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

typedef struct wait_queue wait_queue_t;
typedef struct wait_queue *wait_queue_head_t;
#define DECLARE_WAITQUEUE(wait, current)	struct wait_queue wait = { current, NULL }
#define DECLARE_WAIT_QUEUE_HEAD(wait)		wait_queue_head_t wait
#define init_waitqueue_head(x)			*(x)=NULL
#define init_waitqueue_entry(q,p)		((q)->task)=(p)

#define init_MUTEX(x)				*(x)=MUTEX
#define init_MUTEX_LOCKED(x)			*(x)=MUTEX_LOCKED
#define DECLARE_MUTEX(name)			struct semaphore name=MUTEX
#define DECLARE_MUTEX_LOCKED(name)		struct semaphore name=MUTEX_LOCKED

#define __set_current_state(state_value)	do { current->state = state_value; } while (0)
#ifdef __SMP__
#define set_current_state(state_value)		do { mb(); __set_current_state(state_value); } while (0)
#else
#define set_current_state(state_value)		__set_current_state(state_value)
#endif

#define __exit

#ifdef __alpha
extern long __kernel_thread (unsigned long, int (*)(void *), void *);
static inline long kernel_thread (int (*fn) (void *), void *arg, unsigned long flags)
{
	return __kernel_thread (flags | CLONE_VM, fn, arg);
}
#undef CONFIG_APM
#endif

#define proc_mkdir(buf, usbdir)			create_proc_entry(buf, S_IFDIR, usbdir)

#define pci_enable_device(x)			0

#define VID_HARDWARE_CPIA			24
#define VID_HARDWARE_OV511			27

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

#define tty_register_devfs(driver,flags,minor)
#define tty_unregister_devfs(driver,minor)

#define DECLARE_FSTYPE(var,type,read,flags) \
struct file_system_type var = { \
        name:           type, \
        read_super:     read, \
        fs_flags:       flags, \
}

#define IORESOURCE_IO			1
#define pci_resource_start(dev, i)	(dev->base_address[i] & ~IORESOURCE_IO)
#define pci_resource_len(dev, i)	0x100	/* FIXME */
#define pci_resource_flags(dev, i)	(dev->base_address[i] & IORESOURCE_IO)

#define vmalloc_32			vmalloc
