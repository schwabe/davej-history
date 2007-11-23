/*
 *      Virtual server support for IP masquerading
 *      data structure and funcationality definitions
 */

#ifndef _IP_VS_H
#define _IP_VS_H

#include <linux/config.h>

#ifdef CONFIG_IP_VS_DEBUG
#define IP_VS_DBG(msg...) printk(KERN_DEBUG "IP_VS: " ## msg )
#else	/* NO DEBUGGING at ALL */
#define IP_VS_DBG(msg...)
#endif

#define IP_VS_ERR(msg...) printk(KERN_ERR "IP_VS: " ## msg )
#define IP_VS_INFO(msg...) printk(KERN_INFO "IP_VS: " ## msg )
#define IP_VS_WARNING(msg...) \
	printk(KERN_WARNING "IP_VS: " ## msg)

struct ip_vs_dest;
struct ip_vs_scheduler;

/*
 *	The information about the virtual service offered to the net
 *	and the forwarding entries
 */
struct ip_vs_service {
	struct ip_vs_service    *next;
	__u32                   addr;     /* IP address for virtual service */
	__u16			port;     /* port number for the service */
	__u16			protocol; /* which protocol (TCP/UDP) */
        struct ip_vs_dest 	*destinations; /* real server list */
	struct ip_vs_scheduler 	*scheduler;    /* bound scheduler object */
	void 			*sched_data;   /* scheduler application data */
};


/*
 *	The real server destination forwarding entry
 *	with ip address, port
 */
struct ip_vs_dest {
	struct ip_vs_dest 	*next;
	__u32			addr;     /* IP address of real server */
	__u16			port;     /* port number of the service */
	unsigned		masq_flags; 	/* flags to copy to masq */
	atomic_t		connections;
	atomic_t		refcnt;
	int			weight;
	struct ip_vs_service	*service;	/* service might be NULL */
};


/*
 *	The scheduler object
 */
struct ip_vs_scheduler {
	struct ip_vs_scheduler 	*next;
	char 			*name;
	atomic_t		refcnt;

        /* scheduler initializing service */
	int (*init_service)(struct ip_vs_service *svc);
        /* scheduling service finish */
        int (*done_service)(struct ip_vs_service *svc);

	/* scheduling and creating a masquerading entry */
	struct ip_masq* (*schedule)(struct ip_vs_service *svc, 
				    struct iphdr *iph);
};

/*
 * IP Virtual Server hash table
 */
#define IP_VS_TAB_BITS	CONFIG_IP_MASQUERADE_VS_TAB_BITS
#define IP_VS_TAB_SIZE  (1 << IP_VS_TAB_BITS)
extern struct list_head  ip_vs_table[IP_VS_TAB_SIZE];

/*
 *  Hash and unhash functions
 */
extern int ip_vs_hash(struct ip_masq *ms);
extern int ip_vs_unhash(struct ip_masq *ms);

/*
 *      registering/unregistering scheduler functions
 */
extern int register_ip_vs_scheduler(struct ip_vs_scheduler *scheduler);
extern int unregister_ip_vs_scheduler(struct ip_vs_scheduler *scheduler);

/*
 *  Lookup functions for the hash table
 */
extern struct ip_masq * ip_vs_in_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);
extern struct ip_masq * ip_vs_out_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);

/*
 * Creating a masquerading entry for IPVS
 */
extern struct ip_masq *ip_masq_new_vs(int proto, __u32 maddr, __u16 mport, __u32 saddr, __u16 sport, __u32 daddr, __u16 dport, unsigned flags);

/*
 *      IPVS data and functions
 */
extern rwlock_t __ip_vs_lock;

extern int ip_vs_ctl(int optname, struct ip_masq_ctl *mctl, int optlen);

extern void ip_vs_fin_masq(struct ip_masq *ms);
extern void ip_vs_bind_masq(struct ip_masq *ms, struct ip_vs_dest *dest);
extern void ip_vs_unbind_masq(struct ip_masq *ms);

struct ip_vs_service *ip_vs_lookup_service(__u32 vaddr, __u16 vport,
                                           __u16 protocol);
extern struct ip_masq *ip_vs_schedule(__u32 vaddr, __u16 vport,
				      __u16 protocol,
				      struct iphdr *iph);

extern int ip_vs_tunnel_xmit(struct sk_buff **skb_p, __u32 daddr);

/*
 *      init function
 */
extern int ip_vs_init(void);

/*
 *	init function prototypes for scheduling modules
 *      these function will be called when they are built in kernel
 */
extern int ip_vs_rr_init(void);
extern int ip_vs_wrr_init(void);
extern int ip_vs_wlc_init(void);
extern int ip_vs_pcc_init(void);


/*
 * ip_vs_fwd_tag returns the forwarding tag of the masq
 */
static __inline__ char ip_vs_fwd_tag(struct ip_masq *ms)
{
  char fwd = 'M';

  switch (IP_MASQ_VS_FWD(ms)) {
    case IP_MASQ_F_VS_LOCALNODE: fwd = 'L'; break;
    case IP_MASQ_F_VS_TUNNEL: fwd = 'T'; break;
    case IP_MASQ_F_VS_DROUTE: fwd = 'R'; break;
  }
  return fwd;
}


#endif	/* _IP_VS_H */
