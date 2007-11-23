/*
 * IPVS:        Persistent Client Connection Scheduling module
 *
 * Version:     $Id: ip_vs_pcc.c,v 1.2 1999/07/09 12:12:40 wensong Exp $
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
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <net/ip_masq.h>
#ifdef CONFIG_IP_MASQUERADE_MOD
#include <net/ip_masq_mod.h>
#endif
#include <linux/sysctl.h>
#include <linux/ip_fw.h>
#include <net/ip_vs.h>

/*
 * Note:
 *   It is not very good to make persistent connection client feature
 *   as a sperate scheduling module, because PCC is different from
 *   scheduling modules such as RR, WRR and WLC. In fact, it is good
 *   to let user specify which port is persistent. This will be fixed
 *   in the near future.
 */

/*
 * Define TEMPLATE_TIMEOUT a little larger than average connection time
 * plus MASQUERADE_EXPIRE_TCP_FIN(2*60*HZ). Because the template won't
 * be released until its last controlled masq entry gets expired.
 * If TEMPLATE_TIMEOUT is too less, the template will soon expire and
 * will be put in expire again and again, which requires additional
 * overhead. If it is too large, the same will always visit the same
 * server, which will make dynamic load imbalance worse.
 */
#define TEMPLATE_TIMEOUT	6*60*HZ

static int ip_vs_pcc_init_svc(struct ip_vs_service *svc)
{
        MOD_INC_USE_COUNT;
        return 0;
}


static int ip_vs_pcc_done_svc(struct ip_vs_service *svc)
{
        MOD_DEC_USE_COUNT;
        return 0;
}


/*
 *    In fact, it is Weighted Least Connection scheduling
 */
static struct ip_vs_dest* ip_vs_pcc_select(struct ip_vs_service *svc)
{
	struct ip_vs_dest *dest, *least;
	int loh, doh;

	IP_VS_DBG("ip_vs_pcc_select(): selecting a server...\n");

	if (svc->destinations == NULL) return NULL;

	/*
         * The number of connections in TCP_FIN state is
         *                 dest->refcnt - dest->connections -1
         * We think the overhead of processing active connections is fifty
         * times than that of conncetions in TCP_FIN in average. (This fifty
         * times might be not accurate, we will change it later.) We use
         * the following formula to estimate the overhead:
         *                dest->connections*49 + dest->refcnt
         * and the load:
         *                (dest overhead) / dest->weight
         *
         * Remember -- no floats in kernel mode!!!
         * The comparison of h1*w2 > h2*w1 is equivalent to that of
         *                h1/w1 > h2/w2
         * if every weight is larger than zero.
         */

	least = svc->destinations;
	loh = atomic_read(&least->connections)*49 + atomic_read(&least->refcnt);
        
        /*
         *    Find the destination with the least load.
         */
	for (dest = least->next; dest; dest = dest->next) {
	        doh = atomic_read(&dest->connections)*49 + atomic_read(&dest->refcnt);
	        if (loh*dest->weight > doh*least->weight) {
		        least = dest;
			loh = doh;
		}
	}

        IP_VS_DBG("The selected server: connections %d refcnt %d weight %d"
                  "overhead %d\n", atomic_read(&least->connections),
                  atomic_read(&least->refcnt), least->weight, loh);

	return least;
}


static struct ip_masq* ip_vs_pcc_schedule(struct ip_vs_service *svc, 
					 struct iphdr *iph)
{
	struct ip_masq *ms, *mst;
	struct ip_vs_dest *dest;
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);

	/* check if the template exists */
        mst = ip_masq_in_get(0, iph->saddr, 0, svc->addr, svc->port);
	if (mst) {
		/*
                 * Template masq exists...
                 */
		dest = mst->dest;
                IP_VS_DBG("Template masq fwd:%c s:%s c:%lX:%x v:%lX:%x d:%lX:%x flg:%X cnt:%d\n",
                          ip_vs_fwd_tag(mst), ip_masq_state_name(mst->state),
                          ntohl(mst->daddr),ntohs(mst->dport),
                          ntohl(mst->maddr),ntohs(mst->mport),
                          ntohl(mst->saddr),ntohs(mst->sport),
                          mst->flags, atomic_read(&mst->refcnt));
	} else {
		/* template does not exist, select the destination */
		dest = ip_vs_pcc_select(svc);
		if (!dest) return NULL;

		/* create the template */
		mst = ip_masq_new_vs(0, svc->addr, svc->port,
                                     dest->addr, dest->port,
                                     iph->saddr, 0, 0);
		if (!mst) {
			IP_VS_ERR("ip_masq_new template failed\n");
			return NULL;
		}

                /*
                 *    Bind the template masq entry with the vs dest.
                 */
                ip_vs_bind_masq(mst, dest);
                
                IP_VS_DBG("Template masq created fwd:%c s:%s c:%lX:%x v:%lX:%x"
                          " d:%lX:%x flg:%X cnt:%d\n",
                          ip_vs_fwd_tag(mst), ip_masq_state_name(mst->state),
                          ntohl(mst->daddr),ntohs(mst->dport),
                          ntohl(mst->maddr),ntohs(mst->mport),
                          ntohl(mst->saddr),ntohs(mst->sport),
                          mst->flags, atomic_read(&mst->refcnt));

	}

	/*
         * The destination is known, and create the masq entry
         */
        ms = ip_masq_new_vs(iph->protocol,
                            iph->daddr, portp[1],	
                            dest->addr, dest->port,
                            iph->saddr, portp[0],
                            0);
	if (ms == NULL) {
		IP_VS_ERR("new_vs failed\n");
		return NULL;
	}

        /*
         *    Bind the masq entry with the vs dest.
         */
        ip_vs_bind_masq(ms, dest);
        
	/*
         *    Add its control
         */
        ip_masq_control_add(ms, mst);

        /*
         *    Set the timeout, and put it in expire.
         */
        mst->timeout = TEMPLATE_TIMEOUT;
        ip_masq_put(mst);

        return ms;
}


static struct ip_vs_scheduler ip_vs_pcc_scheduler = {
	NULL,			/* next */
	"pcc",			/* name */
	ATOMIC_INIT(0),		/* refcnt */
	ip_vs_pcc_init_svc,     /* service initializer */
	ip_vs_pcc_done_svc,     /* service done */
	ip_vs_pcc_schedule,     /* select a server and create new masq entry */
};


__initfunc(int ip_vs_pcc_init(void))
{
	IP_VS_INFO("InitialzingPCC scheduling\n");
        return register_ip_vs_scheduler(&ip_vs_pcc_scheduler) ;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	/* module initialization by 'request_module' */
	if(register_ip_vs_scheduler(&ip_vs_pcc_scheduler) != 0)
	        return -EIO;

	IP_VS_INFO("PCC scheduling module loaded.\n");
	
        return 0;
}

void cleanup_module(void)
{
	/* module cleanup by 'release_module' */
	if(unregister_ip_vs_scheduler(&ip_vs_pcc_scheduler) != 0)
	        IP_VS_INFO("cannot remove PCC scheduling module\n");
	else
	        IP_VS_INFO("PCC scheduling module unloaded.\n");
}

#endif /* MODULE */
