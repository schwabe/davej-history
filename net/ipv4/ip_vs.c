/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a part
 *              of IP masquerading code. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Version:     $Id: ip_vs.c,v 1.1.2.1 1999/08/13 18:25:27 davem Exp $
 *
 * Authors:     Wensong Zhang <wensong@iinchina.net>
 *              Peter Kese <peter.kese@ijs.si>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *     Wensong Zhang            :     fixed the overflow bug in ip_vs_procinfo
 *     Wensong Zhang            :     added editing dest and service functions
 *     Wensong Zhang            :     changed name of some functions
 *     Wensong Zhang            :     fixed the unlocking bug in ip_vs_del_dest
 *     Wensong Zhang            :     added a separate hash table for IPVS
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <net/ip_masq.h>

#include <linux/sysctl.h>
#include <linux/ip_fw.h>
#include <linux/ip_masq.h>
#include <linux/proc_fs.h>

#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/route.h>

#include <net/ip_vs.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

EXPORT_SYMBOL(register_ip_vs_scheduler);
EXPORT_SYMBOL(unregister_ip_vs_scheduler);
EXPORT_SYMBOL(ip_vs_bind_masq);
EXPORT_SYMBOL(ip_vs_unbind_masq);

/*
 *  Lock for IPVS
 */
rwlock_t __ip_vs_lock = RW_LOCK_UNLOCKED;

/*
 *  Hash table: for input and output packets lookups of IPVS
 */
struct list_head ip_vs_table[IP_VS_TAB_SIZE];

/*
 * virtual server list and schedulers
 */
static struct ip_vs_service *service_list[2] = {NULL,NULL};
static struct ip_vs_scheduler *schedulers = NULL;


/*
 *  Register a scheduler in the scheduler list
 */
int register_ip_vs_scheduler(struct ip_vs_scheduler *scheduler)
{
	if (!scheduler) {
		IP_VS_ERR("register_ip_vs_scheduler(): NULL arg\n");
		return -EINVAL;
	}

        if (!scheduler->name) {
		IP_MASQ_ERR("register_ip_vs_scheduler(): NULL scheduler_name\n");
		return -EINVAL;
	}

	if (scheduler->next) {
		IP_VS_ERR("register_ip_vs_scheduler(): scheduler already linked\n");
		return -EINVAL;
	}
	
	scheduler->next = schedulers;
	schedulers = scheduler;

	return 0;
}


/*
 *  Unregister a scheduler in the scheduler list
 */
int unregister_ip_vs_scheduler(struct ip_vs_scheduler *scheduler)
{
	struct ip_vs_scheduler **psched;

	if (!scheduler) {
		IP_MASQ_ERR( "unregister_ip_vs_scheduler(): NULL arg\n");
		return -EINVAL;
	}

	/*
	 * 	Only allow unregistration if it is not referenced
	 */
	if (atomic_read(&scheduler->refcnt))  {
		IP_MASQ_ERR( "unregister_ip_vs_scheduler(): is in use by %d guys. failed\n",
				atomic_read(&scheduler->refcnt));
		return -EINVAL;
	}

	/*	
	 *	Must be already removed from the scheduler list
	 */
	for (psched = &schedulers; (*psched) && (*psched != scheduler);
	     psched = &((*psched)->next));

	if (*psched != scheduler) {
		IP_VS_ERR("unregister_ip_vs_scheduler(): scheduler is in the list. failed\n");
		return -EINVAL;
	}

	*psched = scheduler->next;
	scheduler->next = NULL;

	return 0;
}


/*
 *  Bind a service with a scheduler
 */
int ip_vs_bind_scheduler(struct ip_vs_service *svc,
                         struct ip_vs_scheduler *scheduler)
{
        if (svc == NULL) {
		IP_VS_ERR("ip_vs_bind_scheduler(): svc arg NULL\n");
		return -EINVAL;
	}
        if (scheduler == NULL) {
		IP_VS_ERR("ip_vs_bind_scheduler(): scheduler arg NULL\n");
		return -EINVAL;
	}

        svc->scheduler = scheduler;
        atomic_inc(&scheduler->refcnt);
        
        if(scheduler->init_service)
                if(scheduler->init_service(svc) != 0) {
                        IP_VS_ERR("ip_vs_bind_scheduler(): init error\n");
                        return -EINVAL;
                }
        
        return 0;
}


/*
 *  Unbind a service with its scheduler
 */
int ip_vs_unbind_scheduler(struct ip_vs_service *svc)
{
	struct ip_vs_scheduler *sched;

        if (svc == NULL) {
		IP_VS_ERR("ip_vs_unbind_scheduler(): svc arg NULL\n");
		return -EINVAL;
	}

        sched = svc->scheduler;
        if (sched == NULL) {
		IP_VS_ERR("ip_vs_unbind_scheduler(): svc isn't bound\n");
		return -EINVAL;
	}

        if(sched->done_service)
                if(sched->done_service(svc) != 0) {
                        IP_VS_ERR("ip_vs_unbind_scheduler(): done error\n");
                        return -EINVAL;
                }

        atomic_dec(&sched->refcnt);
        svc->scheduler = NULL;

        return 0;
}


/*
 *	Returns hash value for IPVS
 */

static __inline__ unsigned 
ip_vs_hash_key(unsigned proto, __u32 addr, __u16 port)
{
        unsigned addrh = ntohl(addr);
        
        return (proto^addrh^(addrh>>IP_VS_TAB_BITS)^ntohs(port))
                & (IP_VS_TAB_SIZE-1);
}


/*
 *	Hashes ip_masq in ip_vs_table by proto,addr,port.
 *	should be called with locked tables.
 *	returns bool success.
 */
int ip_vs_hash(struct ip_masq *ms)
{
        unsigned hash;

        if (ms->flags & IP_MASQ_F_HASHED) {
                IP_VS_ERR("ip_vs_hash(): request for already hashed, called from %p\n",
                          __builtin_return_address(0));
                return 0;
        }
        /*
         *	Hash by proto,client{addr,port}
         */
        hash = ip_vs_hash_key(ms->protocol, ms->daddr, ms->dport);

        /*
         * Note: because ip_masq_put sets masq expire if its refcnt==2,
         *       we have to increase counter two times, otherwise the
         *       masq won't expire.
         */
	atomic_inc(&ms->refcnt);
	atomic_inc(&ms->refcnt);
        list_add(&ms->m_list, &ip_vs_table[hash]);

        ms->flags |= IP_MASQ_F_HASHED;
        return 1;
}


/*
 *	UNhashes ip_masq from ip_vs_table.
 *	should be called with locked tables.
 *	returns bool success.
 */
int ip_vs_unhash(struct ip_masq *ms)
{
        unsigned int hash;
        struct ip_masq ** ms_p;

        if (!(ms->flags & IP_MASQ_F_HASHED)) {
                IP_VS_ERR("ip_vs_unhash(): request for unhash flagged, called from %p\n",
                          __builtin_return_address(0));
                return 0;
        }
        /*
         *	UNhash by client{addr,port}
         */
        hash = ip_vs_hash_key(ms->protocol, ms->daddr, ms->dport);
        /*
         * Note: since we increase refcnt twice while hashing,
         *       we have to decrease it twice while unhashing.
         */
	atomic_dec(&ms->refcnt);
	atomic_dec(&ms->refcnt);
	list_del(&ms->m_list);
        ms->flags &= ~IP_MASQ_F_HASHED;
        return 1;
}


/*
 *  Gets ip_masq associated with supplied parameters in the ip_vs_table.
 *  Called for pkts coming from OUTside-to-INside the firewall.
 *	s_addr, s_port: pkt source address (foreign host)
 *	d_addr, d_port: pkt dest address (firewall)
 *  Caller must lock tables
 */

struct ip_masq * ip_vs_in_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
        unsigned hash;
        struct ip_masq *ms = NULL;
        struct list_head *l, *e;

        hash = ip_vs_hash_key(protocol, s_addr, s_port);

        l=&ip_vs_table[hash];
        for(e=l->next; e!=l; e=e->next)
	{
		ms = list_entry(e, struct ip_masq, m_list);
		if (protocol==ms->protocol && 
		    d_addr==ms->maddr && d_port==ms->mport &&
		    s_addr==ms->daddr && s_port==ms->dport
		    ) {
			atomic_inc(&ms->refcnt);
                        goto out;
		}
        }

  out:
        return ms;
}


/*
 *  Gets ip_masq associated with supplied parameters in the ip_vs_table.
 *  Called for pkts coming from inside-to-OUTside the firewall.
 *	s_addr, s_port: pkt source address (inside host)
 *	d_addr, d_port: pkt dest address (foreigh host)
 *  Caller must lock tables
 */
struct ip_masq * ip_vs_out_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
        unsigned hash;
        struct ip_masq *ms = NULL;
        struct list_head *l, *e;

	/*	
	 *	Check for "full" addressed entries
	 */
        hash = ip_vs_hash_key(protocol, d_addr, d_port);
        l=&ip_vs_table[hash];

        for(e=l->next; e!=l; e=e->next)
	{	
		ms = list_entry(e, struct ip_masq, m_list);
		if (protocol == ms->protocol &&
		    s_addr == ms->saddr && s_port == ms->sport &&
		    d_addr == ms->daddr && d_port == ms->dport
                    ) {
			atomic_inc(&ms->refcnt);
			goto out;
		}

        }

  out:
        return ms;
}


/*
 *  Create a destination
 */
struct ip_vs_dest *ip_vs_new_dest(struct ip_vs_service *svc,
				  struct ip_masq_ctl *mctl)
{
	struct ip_vs_dest *dest;
	struct ip_vs_user *mm =  &mctl->u.vs_user;

	IP_VS_DBG("enter ip_vs_new_dest()\n");

	dest = (struct ip_vs_dest*) kmalloc(sizeof(struct ip_vs_dest),
					    GFP_ATOMIC);
	if (dest == NULL) {
		IP_VS_ERR("ip_vs_new_dest: kmalloc failed.\n");
		return NULL;
	}
	memset(dest, 0, sizeof(struct ip_vs_dest));

	dest->service = svc;
	dest->addr = mm->daddr;
	dest->port = mm->dport;
	dest->weight = mm->weight;
	dest->masq_flags = mm->masq_flags;

	atomic_set(&dest->connections, 0);
	atomic_set(&dest->refcnt, 0);

        /*
         *    Set the IP_MASQ_F_VS flag
         */
        dest->masq_flags |= IP_MASQ_F_VS;
                
	/* check if local node and update the flags */
	if (inet_addr_type(mm->daddr) == RTN_LOCAL) {
		dest->masq_flags = (dest->masq_flags & ~IP_MASQ_F_VS_FWD_MASK)
                        | IP_MASQ_F_VS_LOCALNODE;
	}

	/* check if (fwd != masquerading) and update the port & flags */
	if ((dest->masq_flags & IP_MASQ_F_VS_FWD_MASK) != 0) {
		dest->masq_flags |= IP_MASQ_F_VS_NO_OUTPUT;
	}

	return dest;
}


/*
 *  Add a destination into an existing service
 */
int ip_vs_add_dest(struct ip_vs_service *svc, struct ip_masq_ctl *mctl)
{
	struct ip_vs_dest *dest;
	struct ip_vs_user *mm =  &mctl->u.vs_user;
        __u32 daddr = mm->daddr;
        __u16 dport = mm->dport;

	IP_VS_DBG("enter ip_vs_add_dest()\n");

	if (mm->weight < 0) {
                IP_VS_ERR("ip_vs_add_dest(): server weight less than zero\n");
                return -ERANGE;
        }

	write_lock_bh(&__ip_vs_lock);

        /* check the existing dest list */
        for (dest=svc->destinations; dest; dest=dest->next) {
                if ((dest->addr == daddr) && (dest->port == dport)) {
                        write_unlock_bh(&__ip_vs_lock);
                        IP_VS_ERR("ip_vs_add_dest(): dest exists\n");
                        return -EEXIST;
                }
        }
        
	/* allocate and initialize the dest structure */
	dest = ip_vs_new_dest(svc, mctl);
	if (dest == NULL) {
                write_unlock_bh(&__ip_vs_lock);
                IP_VS_ERR("ip_vs_add_dest(): out of memory\n");
                return -ENOMEM;
        }
        
	/* put the dest entry into the list */
	dest->next = svc->destinations;
	svc->destinations = dest;
        
	write_unlock_bh(&__ip_vs_lock);

	atomic_inc(&dest->refcnt);

	return 0;
}

        
/*
 *  Edit a destination in a service
 */
int ip_vs_edit_dest(struct ip_vs_service *svc, struct ip_masq_ctl *mctl)
{
	struct ip_vs_dest *dest;
	struct ip_vs_user *mm =  &mctl->u.vs_user;
        __u32 daddr = mm->daddr;
        __u16 dport = mm->dport;

	IP_VS_DBG("enter ip_vs_edit_dest()\n");

	if (mm->weight < 0) {
                IP_VS_ERR("ip_vs_add_dest(): server weight less than zero\n");
                return -ERANGE;
        }
        
	write_lock_bh(&__ip_vs_lock);

        /* lookup the destination list */
        for (dest=svc->destinations; dest; dest=dest->next) {
                if ((dest->addr == daddr) && (dest->port == dport)) {
                        /* HIT */
                        break;
                }
        }

        if (dest == NULL) {
                write_unlock_bh(&__ip_vs_lock);
                IP_VS_ERR("ip_vs_edit_dest(): dest doesn't exist\n");
                return -ENOENT;
        }
        
        /*
         *    Set the weight and the flags
         */
	dest->weight = mm->weight;
	dest->masq_flags = mm->masq_flags;

        dest->masq_flags |= IP_MASQ_F_VS;
                
	/* check if local node and update the flags */
	if (inet_addr_type(mm->daddr) == RTN_LOCAL) {
		dest->masq_flags = (dest->masq_flags & ~IP_MASQ_F_VS_FWD_MASK)
                        | IP_MASQ_F_VS_LOCALNODE;
	}

	/* check if (fwd != masquerading) and update the port & flags */
	if ((dest->masq_flags & IP_MASQ_F_VS_FWD_MASK) != 0) {
		dest->masq_flags |= IP_MASQ_F_VS_NO_OUTPUT;
	}
        
	write_unlock_bh(&__ip_vs_lock);

	return 0;
}


/*
 *  Delete a destination from an existing service
 */
int ip_vs_del_dest(struct ip_vs_service *svc, struct ip_masq_ctl *mctl)
{
        struct ip_vs_dest *dest;
        struct ip_vs_dest **pdest;
	struct ip_vs_user *mm =  &mctl->u.vs_user;
        __u32 daddr = mm->daddr;
        __u16 dport = mm->dport;
        
	IP_VS_DBG("enter ip_vs_del_dest()\n");

	write_lock_bh(&__ip_vs_lock);

	/* remove dest from the destination list */
	pdest = &svc->destinations;
	while (*pdest) {
                dest = *pdest;
                if ((dest->addr == daddr) && (dest->port == dport))
                        /* HIT */
                        break;

                pdest = &dest->next;
        }
        
	if (*pdest == NULL) {
                write_unlock_bh(&__ip_vs_lock);
		IP_VS_ERR("ip_vs_del_dest(): destination not found!\n");
		return -ENOENT;
	}
        
	*pdest = dest->next;
	dest->service = NULL;

	write_unlock_bh(&__ip_vs_lock);

        /*
         *  Decrease the refcnt of the dest, and free the dest
         *  if nobody refers to it (refcnt=0).
         */
        if (atomic_dec_and_test(&dest->refcnt))
                kfree_s(dest, sizeof(*dest));

	return 0;
}
        

#if 0
struct ip_vs_dest * ip_vs_lookup_dest(struct ip_vs_service *svc,
                                      __u32 daddr, __u16 dport)
{
	struct ip_vs_dest *dest;
        
	read_lock_bh(&__ip_vs_lock);

	/*
         * Find the destination for the given service
         */
	for (dest=svc->destinations; dest; dest=dest->next) {
                if ((dest->addr == daddr) && (dest->port == dport)) {
                        /* HIT */
                        read_unlock_bh(&__ip_vs_lock);
                        return dest;
                }
        }

	read_unlock_bh(&__ip_vs_lock);
	return NULL;
}
#endif


/*
 *  Add a service into the service list
 */
int ip_vs_add_service(__u32 vaddr, __u16 vport, 
		      __u16 protocol, struct ip_vs_scheduler *scheduler)
{
	struct ip_vs_service *svc;
	int proto_num = masq_proto_num(protocol);
	int ret = 0;

	write_lock_bh(&__ip_vs_lock);

	/* check if the service already exists */
	for (svc = service_list[proto_num]; svc; svc = svc->next) {
	        if ((svc->port == vport) && (svc->addr == vaddr)) {
		        ret = -EEXIST;
			goto out;
		}
	}

	svc = (struct ip_vs_service*) kmalloc(sizeof(struct ip_vs_service),
					      GFP_ATOMIC);
	if (svc == NULL) {
		IP_VS_ERR("vs_add_svc: kmalloc failed.\n");
		ret = -1;
		goto out;
	}
	memset(svc,0,sizeof(struct ip_vs_service));

	svc->addr = vaddr;
	svc->port = vport;
	svc->protocol = protocol;

        /*
         *    Bind the scheduler
         */
	ip_vs_bind_scheduler(svc, scheduler);


	/* put the service into the proper service list */
	if ((svc->port) || (!service_list[proto_num])) {
		/* prepend to the beginning of the list */
		svc->next = service_list[proto_num];
		service_list[proto_num] = svc;
	} else {
		/* append to the end of the list if port==0 */
		struct ip_vs_service *lsvc = service_list[proto_num];
		while (lsvc->next) lsvc = lsvc->next;
		svc->next = NULL;
		lsvc->next = svc;
	}

  out:
	write_unlock_bh(&__ip_vs_lock);
	return ret;
}


/*
 *  Edit s service
 */
int ip_vs_edit_service(struct ip_vs_service *svc,
                       struct ip_vs_scheduler *scheduler)
{
	write_lock_bh(&__ip_vs_lock);

	/*
         *    Unbind the old scheduler
         */
	ip_vs_unbind_scheduler(svc);

        /*
         *    Bind the new scheduler
         */
	ip_vs_bind_scheduler(svc, scheduler);
        
	write_unlock_bh(&__ip_vs_lock);
        
	return 0;
}


/*
 *  Delete a service from the service list
 */
int ip_vs_del_service(struct ip_vs_service *svc)
{
	struct ip_vs_service **psvc;
        struct ip_vs_dest *dest, *dnext;
	int ret = 0;

	write_lock_bh(&__ip_vs_lock);

	/* remove the service from the service_list */
	psvc = &service_list[masq_proto_num(svc->protocol)];
	for(; *psvc; psvc = &(*psvc)->next) {
		if (*psvc == svc) {
		        break;
		}
	}

	if (*psvc == NULL) {
		IP_VS_ERR("vs_del_svc: service not listed.");
		ret = -1;
		goto out;
	}

	*psvc = svc->next;

	/*
         *    Unbind scheduler
         */
	ip_vs_unbind_scheduler(svc);

        /*
         *    Unlink the destination list
         */
        dest = svc->destinations;
        svc->destinations = NULL;
        for (; dest; dest=dnext) {
                dnext = dest->next;
                dest->service = NULL;
                dest->next = NULL;
                
                /*
                 *  Decrease the refcnt of the dest, and free the dest
                 *  if nobody refers to it (refcnt=0).
                 */
                if (atomic_dec_and_test(&dest->refcnt))
                        kfree_s(dest, sizeof(*dest));
        }

	/*
         *    Free the service
         */
	kfree_s(svc, sizeof(struct ip_vs_service));

  out:
	write_unlock_bh(&__ip_vs_lock);
	return ret;
}


/*
 *  Flush all the virtual services
 */
int ip_vs_flush(void)
{
        int proto_num;
        struct ip_vs_service *svc, *snext;
        struct ip_vs_dest *dest, *dnext;
	int ret = 0;

	write_lock_bh(&__ip_vs_lock);
        
	for (proto_num=0; proto_num<2; proto_num++) {
                svc = service_list[proto_num];
                service_list[proto_num] = NULL;
                for (; svc; svc=snext) {
                        snext = svc->next;

                        /*
                         *    Unbind scheduler
                         */
                        ip_vs_unbind_scheduler(svc);

                        /*
                         *    Unlink the destination list
                         */
                        dest = svc->destinations;
                        svc->destinations = NULL;
                        for (; dest; dest=dnext) {
                                dnext = dest->next;
                                dest->service = NULL;
                                dest->next = NULL;
                
                                /*
                                 *  Decrease the refcnt of the dest, and free
                                 *  the dest if nobody refers to it (refcnt=0).
                                 */
                                if (atomic_dec_and_test(&dest->refcnt))
                                        kfree_s(dest, sizeof(*dest));
                        }

                        /*
                         *    Free the service
                         */
                        kfree_s(svc, sizeof(*svc));
                }
        }
        
	write_unlock_bh(&__ip_vs_lock);
	return ret;
}


/*
 *  Called when a FIN packet of ms is received
 */
void ip_vs_fin_masq(struct ip_masq *ms)
{
        IP_VS_DBG("enter ip_vs_fin_masq()\n");
        
        IP_VS_DBG("Masq fwd:%c s:%s c:%lX:%x v:%lX:%x d:%lX:%x flg:%X cnt:%d\n",
                  ip_vs_fwd_tag(ms), ip_masq_state_name(ms->state),
                  ntohl(ms->daddr),ntohs(ms->dport),
                  ntohl(ms->maddr),ntohs(ms->mport),
                  ntohl(ms->saddr),ntohs(ms->sport),
                  ms->flags, atomic_read(&ms->refcnt));

        if(ms->dest)
                atomic_dec(&ms->dest->connections);
	ms->flags |= IP_MASQ_F_VS_FIN;
}


/*
 *  Bind a masq entry with a VS destination
 */
void ip_vs_bind_masq(struct ip_masq *ms, struct ip_vs_dest *dest)
{
        IP_VS_DBG("enter ip_vs_bind_masq()\n");

        IP_VS_DBG("Masq fwd:%c s:%s c:%lX:%x v:%lX:%x d:%lX:%x flg:%X cnt:%d\n",
                  ip_vs_fwd_tag(ms), ip_masq_state_name(ms->state),
                  ntohl(ms->daddr),ntohs(ms->dport),
                  ntohl(ms->maddr),ntohs(ms->mport),
                  ntohl(ms->saddr),ntohs(ms->sport),
                  ms->flags, atomic_read(&ms->refcnt));

        ms->flags |= dest->masq_flags;
        ms->dest = dest;

        /*
         *    Increase the refcnt and connections couters of the dest.
         */
        atomic_inc(&dest->refcnt);
        atomic_inc(&dest->connections);
}


/*
 *  Unbind a masq entry with its VS destination
 */
void ip_vs_unbind_masq(struct ip_masq *ms)
{
        struct ip_vs_dest *dest = ms->dest;
        
        IP_VS_DBG("enter ip_vs_unbind_masq()\n");

        IP_VS_DBG("Masq fwd:%c s:%s c:%lX:%x v:%lX:%x d:%lX:%x flg:%X cnt:%d\n",
                  ip_vs_fwd_tag(ms), ip_masq_state_name(ms->state),
                  ntohl(ms->daddr),ntohs(ms->dport),
                  ntohl(ms->maddr),ntohs(ms->mport),
                  ntohl(ms->saddr),ntohs(ms->sport),
                  ms->flags, atomic_read(&ms->refcnt));

        if (dest) {
		if (!(ms->flags & IP_MASQ_F_VS_FIN)) {
                        /*
                         * Masq timeout, decrease the connection counter
                         */
			atomic_dec(&dest->connections);
                }
                
                /*
                 *  Decrease the refcnt of the dest, and free the dest
                 *  if nobody refers to it (refcnt=0).
                 */
                if (atomic_dec_and_test(&dest->refcnt))
                        kfree_s(dest, sizeof(*dest));
	}
}


/*
 *    Get scheduler in the scheduler list by name
 */
struct ip_vs_scheduler * ip_vs_sched_getbyname(const char *sched_name)
{
	struct ip_vs_scheduler *sched;

	IP_VS_DBG("ip_vs_sched_getbyname(): sched_name \"%s\"\n", sched_name);
	
	read_lock_bh(&__ip_vs_lock);
	for (sched = schedulers; sched; sched = sched->next) {
	        if (strcmp(sched_name, sched->name)==0) {
			/* HIT */
			read_unlock_bh(&__ip_vs_lock);
                        return sched;
		}
	}

	read_unlock_bh(&__ip_vs_lock);
	return NULL;
}


/*
 *    Lookup scheduler and try to load it if it doesn't exist
 */
struct ip_vs_scheduler * ip_vs_lookup_scheduler(const char *sched_name)
{
	struct ip_vs_scheduler *sched;

        /* search for the scheduler by sched_name */
        sched = ip_vs_sched_getbyname(sched_name);

        /* if scheduler not found, load the module and search again */
        if (sched == NULL) {
                char module_name[IP_MASQ_TNAME_MAX+8];
                sprintf(module_name,"ip_vs_%s",sched_name);
#ifdef CONFIG_KMOD
                request_module(module_name);
#endif /* CONFIG_KMOD */
                sched = ip_vs_sched_getbyname(sched_name);
        }
                        
        return sched;
}


/*
 *  Lookup service by {proto,addr,port} in the service list
 */
struct ip_vs_service *ip_vs_lookup_service(__u32 vaddr, __u16 vport,
                                           __u16 protocol)
{
        struct ip_vs_service *svc;

        read_lock(&__ip_vs_lock);
        svc = service_list[masq_proto_num(protocol)];
        while (svc) {
                if ((svc->addr == vaddr) &&
                    (!svc->port || (svc->port == vport)))
                        break;
                svc = svc->next;
        }
        read_unlock(&__ip_vs_lock);
        return svc; 
}

        
/*
 *  IPVS main scheduling function
 *  It selects a server according to the virtual service, and
 *  creates a masq entry.
 */
struct ip_masq *ip_vs_schedule(__u32 vaddr, __u16 vport, __u16 protocol,
			       struct iphdr *iph)
{
	struct ip_vs_service *svc;
	struct ip_masq *ms = NULL;
	int proto_num = masq_proto_num(protocol);

	read_lock(&__ip_vs_lock);
        
	/*
         * Lookup the service
         */
	for (svc = service_list[proto_num]; svc; svc = svc->next) {
		if ((svc->addr == vaddr) &&
		    (!svc->port || (svc->port == vport))) {
			/*
			 * choose the destination and create ip_masq entry
			 */
			ms = svc->scheduler->schedule(svc, iph);
			break;
		}
	}
        
	read_unlock(&__ip_vs_lock);

        return ms;
}


/*
 *	IPVS user control entry
 */
int ip_vs_ctl(int optname, struct ip_masq_ctl *mctl, int optlen)
{
	struct ip_vs_scheduler *sched = NULL;
        struct ip_vs_service *svc = NULL;
	struct ip_vs_user *mm =  &mctl->u.vs_user;
	__u32 vaddr = mm->vaddr;
	__u16 vport = mm->vport;
	int proto_num = masq_proto_num(mm->protocol);

	/*
	 * Check the size of mctl, no overflow...
	 */
	if (optlen != sizeof(*mctl)) 
		return EINVAL;

	/*
         * Flush all the virtual service...
         */
        if (mctl->m_cmd == IP_MASQ_CMD_FLUSH)
                return ip_vs_flush();

	/*
         * Check for valid protocol: TCP or UDP
         */
        if ((proto_num < 0) || (proto_num > 1)) {
                IP_VS_INFO("vs_ctl: invalid protocol: %d"
                           "%d.%d.%d.%d:%d %s",
                           ntohs(mm->protocol),
                           NIPQUAD(vaddr), ntohs(vport), mctl->m_tname);
                return -EFAULT;
        }

        /*
         * Lookup the service by (vaddr, vport, protocol)
         */
        svc = ip_vs_lookup_service(vaddr, vport, mm->protocol);

        switch (mctl->m_cmd) {
                case IP_MASQ_CMD_ADD:
                        if (svc != NULL)
                                return -EEXIST;

                        /* lookup the scheduler, by 'mctl->m_tname' */
                        sched = ip_vs_lookup_scheduler(mctl->m_tname);
                        if (sched == NULL) {
                                IP_VS_INFO("Scheduler module ip_vs_%s.o not found\n",
                                           mctl->m_tname);
                                return -ENOENT;
                        }

                        return ip_vs_add_service(vaddr, vport,
                                                 mm->protocol, sched);

                case IP_MASQ_CMD_SET:
                        if (svc == NULL)
                                return -ESRCH;

                        /* lookup the scheduler, by 'mctl->m_tname' */
                        sched = ip_vs_lookup_scheduler(mctl->m_tname);
                        if (sched == NULL) {
                                IP_VS_INFO("Scheduler module ip_vs_%s.o not found\n",
                                           mctl->m_tname);
                                return -ENOENT;
                        }

                        return ip_vs_edit_service(svc, sched);
                        
                case IP_MASQ_CMD_DEL:
                        if (svc == NULL)
                                return  -ESRCH;
                        else
                                return ip_vs_del_service(svc);
	
                case IP_MASQ_CMD_ADD_DEST:
                        if (svc == NULL)
                                return  -ESRCH;
                        else
                                return ip_vs_add_dest(svc, mctl);

                case IP_MASQ_CMD_SET_DEST:
                        if (svc == NULL)
                                return  -ESRCH;
                        else
                                return ip_vs_edit_dest(svc, mctl);
                        
                case IP_MASQ_CMD_DEL_DEST:
                        if (svc == NULL)
                                return  -ESRCH;
                        else
                                return ip_vs_del_dest(svc, mctl);
        }
        return -EINVAL;
}



#ifdef CONFIG_PROC_FS
/*
 *	Write the contents of the VS rule table to a PROCfs file.
 */
static int ip_vs_procinfo(char *buf, char **start, off_t offset,
			  int length, int *eof, void *data)
{
	int ind;
        int len=0;
        off_t pos=0;
        int size;
        char str1[22];
	struct ip_vs_service *svc = NULL;
	struct ip_vs_dest *dest;
	__u16 protocol = 0;

	size = sprintf(buf+len,
                       "IP Virtual Server (Version 0.7)\n"
                       "Protocol Local Address:Port Scheduler\n"
                       "      -> Remote Address:Port   Forward Weight ActiveConn FinConn\n");
        pos += size;
        len += size;

	read_lock_bh(&__ip_vs_lock);

        for (ind = 0; ind < 2; ind++) {
                if (ind == 0)
                        protocol = IPPROTO_UDP;
                else
                        protocol = IPPROTO_TCP;

                for (svc=service_list[masq_proto_num(protocol)]; svc; svc=svc->next) {
                        size = sprintf(buf+len, "%s %d.%d.%d.%d:%d %s\n",
                                       masq_proto_name(protocol),
                                       NIPQUAD(svc->addr), ntohs(svc->port),
                                       svc->scheduler->name);
                        len += size;
                        pos += size;

                        if (pos <= offset)
                                len=0;
                        if (pos >= offset+length)
                                goto done;
			       
                        for (dest = svc->destinations; dest; dest = dest->next) {
                                char *fwd;

                                switch (dest->masq_flags & IP_MASQ_F_VS_FWD_MASK) {
                                        case IP_MASQ_F_VS_LOCALNODE:
                                                fwd = "Local";
                                                break;
                                        case IP_MASQ_F_VS_TUNNEL:
                                                fwd = "Tunnel";
                                                break;
                                        case IP_MASQ_F_VS_DROUTE:
                                                fwd = "Route";
                                                break;
                                        default:
                                                fwd = "Masq";
                                }

                                sprintf(str1, "%d.%d.%d.%d:%d",
                                        NIPQUAD(dest->addr), ntohs(dest->port));
                                size = sprintf(buf+len,
                                               "      -> %-21s %-7s %-6d %-10d %-10d\n",
                                               str1, fwd, dest->weight,
                                               atomic_read(&dest->connections),
                                               atomic_read(&dest->refcnt) - atomic_read(&dest->connections) - 1);
                                len += size;
                                pos += size;
                  
                                if (pos <= offset)
                                        len=0;
                                if (pos >= offset+length)
                                        goto done;
                        }
		}
	}

  done:
	read_unlock_bh(&__ip_vs_lock);
        
        *start = buf+len-(pos-offset);          /* Start of wanted data */
        len = pos-offset;
        if (len > length)
                len = length;
        if (len < 0)
                len = 0;
        
	return len;
}

struct proc_dir_entry ip_vs_proc_entry = {
	0,			/* dynamic inode */
	2, "vs",		/* namelen and name */
	S_IFREG | S_IRUGO,	/* mode */
	1, 0, 0, 0,		/* nlinks, owner, group, size */
	&proc_net_inode_operations, /* operations */
	NULL,			/* get_info */
	NULL,			/* fill_inode */
	NULL, NULL, NULL,	/* next, parent, subdir */
	NULL,			/* data */
	&ip_vs_procinfo,	/* function to generate proc data */
};
	
#endif


/*
 *   This function encapsulates the packet in a new IP header, its destination
 *   will be set to the daddr. Most code of this function is from ipip.c.
 *   Usage:
 *     It is called in the ip_fw_demasquerade() function. The load balancer
 *     selects a real server from a cluster based on a scheduling algorithm,
 *     encapsulates the packet and forwards it to the selected server. All real
 *     servers are configured with "ifconfig tunl0 <Virtual IP Address> up".
 *     When the server receives the encapsulated packet, it decapsulates the
 *     packet, processes the request and return the reply packets directly to
 *     the client without passing the load balancer. This can greatly
 *     increase the scalability of virtual server. 
 *   Returns:
 *     if succeeded, return 1; otherwise, return 0.
 */

int ip_vs_tunnel_xmit(struct sk_buff **skb_p, __u32 daddr)
{
 	struct sk_buff *skb = *skb_p;
	struct rtable *rt;     			/* Route to the other host */
	struct device *tdev;			/* Device to other host */
	struct iphdr  *old_iph = skb->nh.iph;
	u8     tos = old_iph->tos;
	u16    df = 0;
	struct iphdr  *iph;			/* Our new IP header */
	int    max_headroom;			/* The extra header space needed */
	u32    dst = daddr;
 	u32    src = 0;
	int    mtu;

	if (skb->protocol != __constant_htons(ETH_P_IP)) {
		IP_VS_ERR("ip_vs_tunnel_xmit(): protocol error, ETH_P_IP: %d, skb protocol: %d\n",
			__constant_htons(ETH_P_IP),skb->protocol);
		goto tx_error;
	}

	if (ip_route_output(&rt, dst, src, RT_TOS(tos), 0)) {
		IP_VS_ERR("ip_vs_tunnel_xmit(): route error, dst: %08X\n", dst);
		goto tx_error_icmp;
	}
	tdev = rt->u.dst.dev;

	mtu = rt->u.dst.pmtu - sizeof(struct iphdr);
	if (mtu < 68) {
		ip_rt_put(rt);
		IP_VS_ERR("ip_vs_tunnel_xmit(): mtu less than 68\n");
		goto tx_error;
	}
	if (skb->dst && mtu < skb->dst->pmtu)
		skb->dst->pmtu = mtu;

	df |= (old_iph->frag_off&__constant_htons(IP_DF));

	if ((old_iph->frag_off&__constant_htons(IP_DF)) && mtu < ntohs(old_iph->tot_len)) {
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
		ip_rt_put(rt);
		IP_VS_ERR("ip_vs_tunnel_xmit(): frag needed\n");
		goto tx_error;
	}

	skb->h.raw = skb->nh.raw;

	/*
	 * Okay, now see if we can stuff it in the buffer as-is.
	 */
	max_headroom = (((tdev->hard_header_len+15)&~15)+sizeof(struct iphdr));

	if (skb_headroom(skb) < max_headroom || skb_cloned(skb) || skb_shared(skb)) {
		struct sk_buff *new_skb = skb_realloc_headroom(skb, max_headroom);
		if (!new_skb) {
			ip_rt_put(rt);
			kfree_skb(skb);
			IP_VS_ERR("ip_vs_tunnel_xmit(): no memory for new_skb\n");
			return 0;
		}
		kfree_skb(skb);
		skb = new_skb;
	}

	skb->nh.raw = skb_push(skb, sizeof(struct iphdr));
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	dst_release(skb->dst);
	skb->dst = &rt->u.dst;

	/*
	 *	Push down and install the IPIP header.
	 */

	iph 			=	skb->nh.iph;
	iph->version		=	4;
	iph->ihl		=	sizeof(struct iphdr)>>2;
	iph->frag_off		=	df;
	iph->protocol		=	IPPROTO_IPIP;
	iph->tos		=	tos;
	iph->daddr		=	rt->rt_dst;
	iph->saddr		=	rt->rt_src;
	iph->ttl		=	old_iph->ttl;
	iph->tot_len		=	htons(skb->len);
	iph->id			=	htons(ip_id_count++);
	ip_send_check(iph);

	ip_send(skb);
	return 1;

tx_error_icmp:
	dst_link_failure(skb);
tx_error:
	kfree_skb(skb);
	return 0;
}


/*
 *	Initialize IP virtual server
 */
__initfunc(int ip_vs_init(void))
{
	int idx;
        for(idx = 0; idx < IP_VS_TAB_SIZE; idx++)  {
		INIT_LIST_HEAD(&ip_vs_table[idx]);
	}
#ifdef CONFIG_PROC_FS
	ip_masq_proc_register(&ip_vs_proc_entry);	
#endif        

#ifdef CONFIG_IP_MASQUERADE_VS_RR
        ip_vs_rr_init();
#endif
#ifdef CONFIG_IP_MASQUERADE_VS_WRR
        ip_vs_wrr_init();
#endif
#ifdef CONFIG_IP_MASQUERADE_VS_WLC
        ip_vs_wlc_init();
#endif
#ifdef CONFIG_IP_MASQUERADE_VS_WLC
        ip_vs_pcc_init();
#endif
        return 0;
}
