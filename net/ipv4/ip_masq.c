/*
 *
 * 	Masquerading functionality
 *
 * 	Copyright (c) 1994 Pauline Middelink
 *
 *	See ip_fw.c for original log
 *
 * Fixes:
 *	Juan Jose Ciarlante	:	Modularized application masquerading (see ip_masq_app.c)
 *	Juan Jose Ciarlante	:	New struct ip_masq_seq that holds output/input delta seq.
 *	Juan Jose Ciarlante	:	Added hashed lookup by proto,maddr,mport and proto,saddr,sport
 *	Juan Jose Ciarlante	:	Fixed deadlock if free ports get exhausted
 *	Juan Jose Ciarlante	:	Added NO_ADDR status flag.
 *	Richard Lynch		:	Added IP Autoforward
 *	Nigel Metheringham	:	Added ICMP handling for demasquerade
 *	Nigel Metheringham	:	Checksum checking of masqueraded data
 *	Nigel Metheringham	:	Better handling of timeouts of TCP conns
 *	Keith Owens		:	Keep control channels alive if any related data entries.
 *	Delian Delchev		:	Added support for ICMP requests and replys
 *	Nigel Metheringham	:	ICMP in ICMP handling, tidy ups, bug fixes, made ICMP optional
 *	Juan Jose Ciarlante	:	re-assign maddr if no packet received from outside
 * 	John D. Hardin		:	Added PPTP and IPSEC protocols
 *	
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <asm/system.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/inet.h>
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/checksum.h>
#include <net/ip_masq.h>
#include <linux/ip_fw.h>

#define IP_MASQ_TAB_SIZE 256    /* must be power of 2 */

#ifdef CONFIG_IP_MASQUERADE_PPTP
/*
 * This is clumsier than it otherwise might be (i.e. the
 * PPTP control channel sniffer should be a module, and there
 * should be a separate table for GRE masq entries so that
 * we're not making all of the hacks to the TCP table code)
 # but I wanted to keep the code changes localized to one file
 # if possible.
 * This should all be modular, and the table routines need to
 * be somewhat more generic.
 *
 * Maybe for 2.0.38 - we'll see.
 *
 * John Hardin <jhardin@wolfenet.com> gets all blame...
 * See also http://www.wolfenet.com/~jhardin/ip_masq_vpn.html
 */

static const char *strGREProt = "GRE";

#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
/*
 * MULTICLIENT watches the control channel and preloads the
 * call ID into the masq table entry, so we want the
 * masq table entry to persist until a Call Disconnect
 * occurs, otherwise the call IDs will be lost and the link broken.
 */
#define MASQUERADE_EXPIRE_PPTP 15*60*HZ

/*
 * To support multiple clients communicating with the same server,
 * we have to sniff the control channel and trap the client's
 * call ID, then substitute a unique-to-the-firewall call ID.
 * Then on inbound GRE packets we use the bogus call ID to figure
 * out which client to route the traffic to, then replace the
 * bogus call ID with the client's real call ID, which we've saved.
 * For simplicity we'll use masq port as the bogus call ID.
 * The actual call ID will be stored in the masq table as
 * the source port, and the destination port will always be zero.
 *
 * NB: PPTP servers can tell whether the client is masqueraded by
 * looking for call IDs above 61000.
 */
#define PPTP_CONTROL_PORT 1723

#else /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */

/* non-MULTICLIENT ignores call IDs, so masq table
 * entries may expire quickly without causing problems.
 */
#define MASQUERADE_EXPIRE_PPTP 5*60*HZ

#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */

/*
 * Define this here rather than in /usr/src/linux/include/wherever/whatever.h
 * in order to localize my mistakes to one file...
 *
 * This struct may be architecture-specific because of the bitmaps.
 */
struct pptp_gre_header {
        __u8
                recur:3,
                is_strict:1,
                has_seq:1,
                has_key:1,
                has_routing:1,
                has_cksum:1;
        __u8
                version:3,
                flags:5;
        __u16   
                protocol,
                payload_len,
                call_id;        /* peer's call_id for this session */

};

#endif /* CONFIG_IP_MASQUERADE_PPTP */


#ifdef CONFIG_IP_MASQUERADE_IPSEC
/*
 * The above comments about PPTP apply here, too. This should all be a module.
 *
 * The "port numbers" for masq table purposes will be part of the
 * SPI, just to gain a little benefit from the hashing.
 */

static const char *strESPProt = "ESP";
static const char *strAHProt = "AH";

/*
 * ISAKMP uses 500/udp, and the traffic must come from
 * 500/udp (i.e. 500/udp <-> 500/udp), so we need to
 * check for ISAKMP UDP traffic and avoid changing the
 * source port number. In order to associate the data streams
 * we may need to sniff the ISAKMP cookies as well.
 */
#define UDP_PORT_ISAKMP	500	/* ISAKMP default UDP port */

#if CONFIG_IP_MASQUERADE_IPSEC_EXPIRE > 15
#define MASQUERADE_EXPIRE_IPSEC CONFIG_IP_MASQUERADE_IPSEC_EXPIRE*60*HZ
#else
#define MASQUERADE_EXPIRE_IPSEC 15*60*HZ
#endif

/*
 * We can't know the inbound SPI until it comes in (the ISAKMP exchange
 * is encryptd so we can't sniff it out of that), so we associate inbound
 * and outbound traffic by inspection. If somebody sends a new packet to a
 * remote server, then block all other new traffic to that server until we
 * get a response from that server with a SPI we haven't seen yet. It is
 * assumed that this is the correct response - we have no way to verify it,
 * as everything else is encrypted.
 *
 * If there is a collision, the block will last for up to two minutes (or
 * whatever MASQUERADE_EXPIRE_IPSEC_INIT is set to), and if the client
 * retries during that time the timer will be reset. This could easily lead
 * to a Denial of Service, so we limit the number of retries that will
 * reset the timer. This means the maximum time the server could be blocked
 * is ((IPSEC_INIT_RETRIES + 1) * MASQUERADE_EXPIRE_IPSEC_INIT).
 *
 * Note: blocking will not affect already-established traffic (i.e. where
 * the inbound SPI has been associated with an outbound SPI).
 */
#define MASQUERADE_EXPIRE_IPSEC_INIT 2*60*HZ
#define IPSEC_INIT_RETRIES 5

/*
 * ...connections that don't get an answer are squelched
 * (recognized but ignored) for a short time to prevent DoS.
 * SPI values 1-255 are reserved by the IANA and are currently (2/99)
 * not assigned. If that should change, this number must also be changed
 * to an unused NONZERO value:
 */
#define IPSEC_INIT_SQUELCHED 1

struct ip_masq * ip_masq_out_get_ipsec(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port, __u32 o_spi);
struct ip_masq * ip_masq_in_get_ipsec(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port, __u32 i_spi);
struct ip_masq * ip_masq_out_get_isakmp(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port, __u32 cookie);
struct ip_masq * ip_masq_in_get_isakmp(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port, __u32 cookie);

#endif /* CONFIG_IP_MASQUERADE_IPSEC */

/*
 *	Implement IP packet masquerading
 */

static const char *strProt[] = {"UDP","TCP","ICMP"};

/*
 * masq_proto_num returns 0 for UDP, 1 for TCP, 2 for ICMP
 *
 * No, I am NOT going to add GRE/ESP/AH support to everything that relies on this...
 *
 */

static int masq_proto_num(unsigned proto)
{
   switch (proto)
   {
      case IPPROTO_UDP:  return (0); break;
#ifdef CONFIG_IP_MASQUERADE_PPTP
      case IPPROTO_GRE:
#endif /* CONFIG_IP_MASQUERADE_PPTP */
#ifdef CONFIG_IP_MASQUERADE_IPSEC
      case IPPROTO_ESP:
#endif /* CONFIG_IP_MASQUERADE_IPSEC */
      case IPPROTO_TCP:  return (1); break;
      case IPPROTO_ICMP: return (2); break;
      default:           return (-1); break;
   }
}

#ifdef CONFIG_IP_MASQUERADE_ICMP
/*
 * Converts an ICMP reply code into the equivalent request code
 */
static __inline__ const __u8 icmp_type_request(__u8 type)
{
   switch (type)
   {
      case ICMP_ECHOREPLY: return ICMP_ECHO; break;
      case ICMP_TIMESTAMPREPLY: return ICMP_TIMESTAMP; break;
      case ICMP_INFO_REPLY: return ICMP_INFO_REQUEST; break;
      case ICMP_ADDRESSREPLY: return ICMP_ADDRESS; break;
      default: return (255); break;
   }
}

/*
 * Helper macros - attempt to make code clearer! 
 */

/* ID used in ICMP lookups */
#define icmp_id(icmph)		((icmph->un).echo.id)
/* (port) hash value using in ICMP lookups for requests */
#define icmp_hv_req(icmph)	((__u16)(icmph->code+(__u16)(icmph->type<<8)))
/* (port) hash value using in ICMP lookups for replies */
#define icmp_hv_rep(icmph)	((__u16)(icmph->code+(__u16)(icmp_type_request(icmph->type)<<8)))
#endif

static __inline__ const char *masq_proto_name(unsigned proto)
{

	/*
	 * I don't want to track down everything that
	 * relies on masq_proto_num() and make it GRE/ESP/AH-tolerant.
	 */
#ifdef CONFIG_IP_MASQUERADE_PPTP
	if (proto == IPPROTO_GRE) {
          return strGREProt;
	}
#endif /* CONFIG_IP_MASQUERADE_PPTP */
#ifdef CONFIG_IP_MASQUERADE_IPSEC
	if (proto == IPPROTO_ESP) {
          return strESPProt;
	} else if (proto == IPPROTO_AH) {
          return strAHProt;
	}
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

        return strProt[masq_proto_num(proto)];
}

/*
 *	Last masq_port number in use.
 *	Will cycle in MASQ_PORT boundaries.
 */
static __u16 masq_port = PORT_MASQ_BEGIN;

/*
 *	free ports counters (UDP & TCP)
 *
 *	Their value is _less_ or _equal_ to actual free ports:
 *	same masq port, diff masq addr (firewall iface address) allocated
 *	entries are accounted but their actually don't eat a more than 1 port.
 *
 *	Greater values could lower MASQ_EXPIRATION setting as a way to
 *	manage 'masq_entries resource'.
 *	
 */

int ip_masq_free_ports[3] = {
        PORT_MASQ_END - PORT_MASQ_BEGIN, 	/* UDP */
        PORT_MASQ_END - PORT_MASQ_BEGIN, 	/* TCP */
        PORT_MASQ_END - PORT_MASQ_BEGIN		/* ICMP */
};

static struct symbol_table ip_masq_syms = {
#include <linux/symtab_begin.h>
	X(ip_masq_new),
        X(ip_masq_set_expire),
        X(ip_masq_free_ports),
	X(ip_masq_expire),
	X(ip_masq_out_get_2),
#include <linux/symtab_end.h>
};

/*
 *	2 ip_masq hash tables: for input and output pkts lookups.
 */

struct ip_masq *ip_masq_m_tab[IP_MASQ_TAB_SIZE];
struct ip_masq *ip_masq_s_tab[IP_MASQ_TAB_SIZE];

#ifdef CONFIG_IP_MASQUERADE_IPSEC
	/*
	 * Add a third hash table for input lookup by remote side
	 */
struct ip_masq *ip_masq_d_tab[IP_MASQ_TAB_SIZE];

#endif /* CONFIG_IP_MASQUERADE_IPSEC */

/*
 * timeouts
 */

static struct ip_fw_masq ip_masq_dummy = {
	MASQUERADE_EXPIRE_TCP,
	MASQUERADE_EXPIRE_TCP_FIN,
	MASQUERADE_EXPIRE_UDP
};

struct ip_fw_masq *ip_masq_expire = &ip_masq_dummy;

#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW
/*
 *	Auto-forwarding table
 */

struct ip_autofw * ip_autofw_hosts = NULL;

/*
 *	Check if a masq entry should be created for a packet
 */

struct ip_autofw * ip_autofw_check_range (__u32 where, __u16 port, __u16 protocol, int reqact)
{
	struct ip_autofw *af;
	af=ip_autofw_hosts;
	port=ntohs(port);
	while (af)
	{
		if (af->type==IP_FWD_RANGE && 
		     port>=af->low && 
		     port<=af->high && 
		     protocol==af->protocol && 
		     /* it's ok to create masq entries after the timeout if we're in insecure mode */
		     (af->flags & IP_AUTOFW_ACTIVE || !reqact || !(af->flags & IP_AUTOFW_SECURE)) &&  
		     (!(af->flags & IP_AUTOFW_SECURE) || af->lastcontact==where || !reqact))
			return(af);
		af=af->next;
	}
	return(NULL);
}

struct ip_autofw * ip_autofw_check_port (__u16 port, __u16 protocol)
{
	struct ip_autofw *af;
	af=ip_autofw_hosts;
	port=ntohs(port);
	while (af)
	{
		if (af->type==IP_FWD_PORT && port==af->visible && protocol==af->protocol)
			return(af);
		af=af->next;
	}
	return(NULL);
}

struct ip_autofw * ip_autofw_check_direct (__u16 port, __u16 protocol)
{
	struct ip_autofw *af;
	af=ip_autofw_hosts;
	port=ntohs(port);
	while (af)
	{
		if (af->type==IP_FWD_DIRECT && af->low<=port && af->high>=port)
			return(af);
		af=af->next;
	}
	return(NULL);
}

void ip_autofw_update_out (__u32 who, __u32 where, __u16 port, __u16 protocol)
{
	struct ip_autofw *af;
	af=ip_autofw_hosts;
	port=ntohs(port);
	while (af)
	{
		if (af->type==IP_FWD_RANGE && af->ctlport==port && af->ctlproto==protocol)
		{
			if (af->flags & IP_AUTOFW_USETIME)
			{
				if (af->timer.expires)
					del_timer(&af->timer);
				af->timer.expires=jiffies+IP_AUTOFW_EXPIRE;
				add_timer(&af->timer);
			}
			af->flags|=IP_AUTOFW_ACTIVE;
			af->lastcontact=where;
			af->where=who;
		}
		af=af->next;
	}
}

void ip_autofw_update_in (__u32 where, __u16 port, __u16 protocol)
{
/*	struct ip_autofw *af;
	af=ip_autofw_check_range(where, port,protocol);
	if (af)
	{
		del_timer(&af->timer);
		af->timer.expires=jiffies+IP_AUTOFW_EXPIRE;
		add_timer(&af->timer);
	}*/
}

#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */

/*
 *	Returns hash value
 */

static __inline__ unsigned
ip_masq_hash_key(unsigned proto, __u32 addr, __u16 port)
{
        return (proto^ntohl(addr)^ntohs(port)) & (IP_MASQ_TAB_SIZE-1);
}

/*
 *	Hashes ip_masq by its proto,addrs,ports.
 *	should be called with masked interrupts.
 *	returns bool success.
 */

static __inline__ int
ip_masq_hash(struct ip_masq *ms)
{
        unsigned hash;

        if (ms->flags & IP_MASQ_F_HASHED) {
                printk("ip_masq_hash(): request for already hashed\n");
                return 0;
        }
        /*
         *	Hash by proto,m{addr,port}
         */
        hash = ip_masq_hash_key(ms->protocol, ms->maddr, ms->mport);
        ms->m_link = ip_masq_m_tab[hash];
        ip_masq_m_tab[hash] = ms;

        /*
         *	Hash by proto,s{addr,port}
         */
#ifdef CONFIG_IP_MASQUERADE_PPTP
	if (ms->protocol == IPPROTO_GRE) {
                /* Ignore the source port (Call ID) when hashing, as
                 * outbound packets will not be able to supply it...
                 */
                hash = ip_masq_hash_key(ms->protocol, ms->saddr, 0);
        } else
#endif /* CONFIG_IP_MASQUERADE_PPTP */
        hash = ip_masq_hash_key(ms->protocol, ms->saddr, ms->sport);
        ms->s_link = ip_masq_s_tab[hash];
        ip_masq_s_tab[hash] = ms;

#ifdef CONFIG_IP_MASQUERADE_IPSEC
        /* 
          * Hash by proto,d{addr,port}
          */
        hash = ip_masq_hash_key(ms->protocol, ms->daddr, ms->dport);
	ms->d_link = ip_masq_d_tab[hash];
	ip_masq_d_tab[hash] = ms;
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

        ms->flags |= IP_MASQ_F_HASHED;
        return 1;
}

/*
 *	UNhashes ip_masq from ip_masq_[ms]_tables.
 *	should be called with masked interrupts.
 *	returns bool success.
 */

static __inline__ int ip_masq_unhash(struct ip_masq *ms)
{
        unsigned hash;
        struct ip_masq ** ms_p;
        if (!(ms->flags & IP_MASQ_F_HASHED)) {
                printk("ip_masq_unhash(): request for unhash flagged\n");
                return 0;
        }
        /*
         *	UNhash by m{addr,port}
         */
        hash = ip_masq_hash_key(ms->protocol, ms->maddr, ms->mport);
        for (ms_p = &ip_masq_m_tab[hash]; *ms_p ; ms_p = &(*ms_p)->m_link)
                if (ms == (*ms_p))  {
                        *ms_p = ms->m_link;
                        break;
                }
        /*
         *	UNhash by s{addr,port}
         */
#ifdef CONFIG_IP_MASQUERADE_PPTP
	if (ms->protocol == IPPROTO_GRE) {
                hash = ip_masq_hash_key(ms->protocol, ms->saddr, 0);
        } else
#endif /* CONFIG_IP_MASQUERADE_PPTP */
        hash = ip_masq_hash_key(ms->protocol, ms->saddr, ms->sport);
        for (ms_p = &ip_masq_s_tab[hash]; *ms_p ; ms_p = &(*ms_p)->s_link)
                if (ms == (*ms_p))  {
                        *ms_p = ms->s_link;
                        break;
                }

#ifdef CONFIG_IP_MASQUERADE_IPSEC
	/* 
	 * UNhash by d{addr,port}
	 */
	hash = ip_masq_hash_key(ms->protocol, ms->daddr, ms->dport);
	for (ms_p = &ip_masq_d_tab[hash]; *ms_p ; ms_p = &(*ms_p)->d_link)
		if (ms == (*ms_p))  {
			*ms_p = ms->d_link;
			break;
		}
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

        ms->flags &= ~IP_MASQ_F_HASHED;
        return 1;
}

/*
 *	Returns ip_masq associated with addresses found in iph.
 *	called for pkts coming from outside-to-INside the firewall
 *
 * 	NB. Cannot check destination address, just for the incoming port.
 * 	reason: archie.doc.ac.uk has 6 interfaces, you send to
 * 	phoenix and get a reply from any other interface(==dst)!
 *
 * 	[Only for UDP] - AC
 */

struct ip_masq *
ip_masq_in_get(struct iphdr *iph)
{
 	__u16 *portptr;
        int protocol;
        __u32 s_addr, d_addr;
        __u16 s_port, d_port;
#ifdef CONFIG_IP_MASQUERADE_IPSEC
        __u32 cookie;
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

 	portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
        protocol = iph->protocol;
        s_addr = iph->saddr;
        s_port = portptr[0];
        d_addr = iph->daddr;
        d_port = portptr[1];

#ifdef CONFIG_IP_MASQUERADE_IPSEC
        if (protocol == IPPROTO_UDP && ntohs(s_port) == UDP_PORT_ISAKMP && ntohs(d_port) == UDP_PORT_ISAKMP) {
                cookie = *((__u32 *)&portptr[4]);
                return ip_masq_in_get_isakmp(protocol, s_addr, s_port, d_addr, d_port, cookie);
        } else
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

        return ip_masq_in_get_2(protocol, s_addr, s_port, d_addr, d_port);
}

/*
 *	Returns ip_masq associated with supplied parameters, either
 *	broken out of the ip/tcp headers or directly supplied for those
 *	pathological protocols with address/port in the data stream
 *	(ftp, irc).  addresses and ports are in network order.
 *	called for pkts coming from outside-to-INside the firewall.
 *
 * 	NB. Cannot check destination address, just for the incoming port.
 * 	reason: archie.doc.ac.uk has 6 interfaces, you send to
 * 	phoenix and get a reply from any other interface(==dst)!
 *
 * 	[Only for UDP] - AC
 */

struct ip_masq *
ip_masq_in_get_2(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
        unsigned hash;
        struct ip_masq *ms;

        hash = ip_masq_hash_key(protocol, d_addr, d_port);
        for(ms = ip_masq_m_tab[hash]; ms ; ms = ms->m_link) {
 		if (protocol==ms->protocol &&
		    ((s_addr==ms->daddr || ms->flags & IP_MASQ_F_NO_DADDR)
#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW
		     || (ms->dport==htons(1558))
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */
		     ) &&
		    (s_port==ms->dport || ms->flags & IP_MASQ_F_NO_DPORT) &&
		    (d_addr==ms->maddr && d_port==ms->mport)) {
#ifdef DEBUG_IP_MASQUERADE_VERBOSE
			printk("MASQ: look/in %d %08X:%04hX->%08X:%04hX OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port);
#endif
                        return ms;
		}
        }

#ifdef CONFIG_IP_MASQUERADE_PPTP
	if (protocol == IPPROTO_GRE) {
                for(ms = ip_masq_m_tab[hash]; ms ; ms = ms->m_link) {
                        if (protocol==ms->protocol &&
#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
                            ms->mport == d_port && /* ignore source port */
#else /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
                            ms->mport == 0 && ms->sport == 0 &&
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
                            s_addr==ms->daddr && d_addr==ms->maddr) {
#ifdef DEBUG_IP_MASQUERADE_PPTP_VERBOSE
                                printk(KERN_DEBUG "MASQ: look/in %d %08X:%04hX->%08X:%04hX OK\n",
                                       protocol,
                                       s_addr,
                                       s_port,
                                       d_addr,
                                       d_port);
#endif /* DEBUG_IP_MASQUERADE_PPTP_VERBOSE */
                                return ms;
                        }
		}
        }
#endif /* CONFIG_IP_MASQUERADE_PPTP */


#ifdef DEBUG_IP_MASQUERADE_VERBOSE
	printk("MASQ: look/in %d %08X:%04hX->%08X:%04hX fail\n",
	       protocol,
	       s_addr,
	       s_port,
	       d_addr,
	       d_port);
#endif
        return NULL;
}

#ifdef CONFIG_IP_MASQUERADE_IPSEC
struct ip_masq *
ip_masq_in_get_ipsec(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port, __u32 i_spi)
{
        unsigned hash;
        struct ip_masq *ms;

        if (protocol != IPPROTO_ESP) {
                return ip_masq_in_get_2(protocol,s_addr,s_port,d_addr,d_port);
        }

        /* find an entry for a packet coming in from outside,
         * or find whether there's a setup pending
         */

        if (i_spi != 0) {
                /* there's a SPI - look for a completed entry */
                hash = ip_masq_hash_key(protocol, s_addr, s_port);
                for(ms = ip_masq_d_tab[hash]; ms ; ms = ms->d_link) {
                        if (protocol==ms->protocol &&
                            s_addr==ms->daddr &&
                            d_addr==ms->maddr &&
                            ms->ispi != 0 && i_spi==ms->ispi) {
#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
                                printk(KERN_DEBUG "MASQ: IPSEC look/in %08X->%08X:%08X OK\n",
                                       s_addr,
                                       d_addr,
                                       i_spi);
#endif
                                return ms;
                        }
                }
        }
        
        /* no joy. look for a pending connection - maybe somebody else's
         * if we're checking for a pending setup, the d_addr will be zero
         * to avoid having to know the masq IP.
         */
        hash = ip_masq_hash_key(protocol, s_addr, 0);
        for(ms = ip_masq_d_tab[hash]; ms ; ms = ms->d_link) {
                if (protocol==ms->protocol &&
                    s_addr==ms->daddr &&
                    (d_addr==0 || d_addr==ms->maddr) &&
                    ms->ispi==0) {
#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
                        printk(KERN_DEBUG "MASQ: IPSEC look/in %08X->%08X:0 OK\n",
                               s_addr,
                               d_addr
                               );
#endif
                        return ms;
                }
        }

#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
	printk(KERN_DEBUG "MASQ: IPSEC look/in %08X->%08X:%08X fail\n",
	       s_addr,
	       d_addr,
	       i_spi);
#endif
        return NULL;
}

struct ip_masq *
ip_masq_in_get_isakmp(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port, __u32 cookie)
{
        unsigned hash;
        struct ip_masq *ms;

#ifdef DEBUG_IP_MASQUERADE_IPSEC
	printk(KERN_DEBUG "ip_masq_in_get_isakmp(): ");
	printk("%s -> ", in_ntoa(s_addr));
	printk("%s cookie %lX\n", in_ntoa(d_addr), ntohl(cookie));
#endif /* DEBUG_IP_MASQUERADE_IPSEC */

        if (cookie == 0) {
                printk(KERN_INFO "ip_masq_in_get_isakmp(): ");
                printk("zero cookie from %s\n", in_ntoa(s_addr));
        }

        hash = ip_masq_hash_key(protocol, d_addr, d_port);
        for(ms = ip_masq_m_tab[hash]; ms ; ms = ms->m_link) {
 		if (protocol==ms->protocol &&
		    cookie==ms->ospi &&
		    ((s_addr==ms->daddr || ms->flags & IP_MASQ_F_NO_DADDR)
		     ) &&
		    (s_port==ms->dport || ms->flags & IP_MASQ_F_NO_DPORT) &&
		    (d_addr==ms->maddr && d_port==ms->mport)) {
#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
			printk(KERN_DEBUG "MASQ: look/in %d %08X:%04hX->%08X:%04hX %08X OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port,
                               cookie);
#endif
                        return ms;
		}
        }

#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
	printk(KERN_DEBUG "MASQ: look/in %d %08X:%04hX->%08X:%04hX %08X fail\n",
	       protocol,
	       s_addr,
	       s_port,
	       d_addr,
	       d_port,
               cookie);
#endif
        return NULL;
}

#endif /* CONFIG_IP_MASQUERADE_IPSEC */

/*
 *	Returns ip_masq associated with addresses found in iph.
 *	called for pkts coming from inside-to-OUTside the firewall.
 */

struct ip_masq *
ip_masq_out_get(struct iphdr *iph)
{
 	__u16 *portptr;
        int protocol;
        __u32 s_addr, d_addr;
        __u16 s_port, d_port;
#ifdef CONFIG_IP_MASQUERADE_IPSEC
        __u32 cookie;
#endif /* CONFIG_IP_MASQUERADE_IPSEC */


 	portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
        protocol = iph->protocol;
        s_addr = iph->saddr;
        s_port = portptr[0];
        d_addr = iph->daddr;
        d_port = portptr[1];

#ifdef CONFIG_IP_MASQUERADE_IPSEC
        if (protocol == IPPROTO_UDP && ntohs(s_port) == UDP_PORT_ISAKMP && ntohs(d_port) == UDP_PORT_ISAKMP) {
                cookie = *((__u32 *)&portptr[4]);
                return ip_masq_out_get_isakmp(protocol, s_addr, s_port, d_addr, d_port, cookie);
        } else
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

        return ip_masq_out_get_2(protocol, s_addr, s_port, d_addr, d_port);
}

/*
 *	Returns ip_masq associated with supplied parameters, either
 *	broken out of the ip/tcp headers or directly supplied for those
 *	pathological protocols with address/port in the data stream
 *	(ftp, irc).  addresses and ports are in network order.
 *	called for pkts coming from inside-to-OUTside the firewall.
 *
 *	Normally we know the source address and port but for some protocols
 *	(e.g. ftp PASV) we do not know the source port initially.  Alas the
 *	hash is keyed on source port so if the first lookup fails then try again
 *	with a zero port, this time only looking at entries marked "no source
 *	port".
 */

struct ip_masq *
ip_masq_out_get_2(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
        unsigned hash;
        struct ip_masq *ms;


#ifdef CONFIG_IP_MASQUERADE_PPTP
#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
	if (protocol == IPPROTO_GRE) {
                /*
                 * Call ID is saved in source port number,
                 * but we have no way of knowing it on the outbound packet...
                 * we only know the *other side's* Call ID
                 */

                hash = ip_masq_hash_key(protocol, s_addr, 0);
                for(ms = ip_masq_s_tab[hash]; ms ; ms = ms->s_link) {
                        if (protocol == ms->protocol &&
                            s_addr == ms->saddr && (s_port == 0 || s_port == ms->sport) &&
                            d_addr == ms->daddr && d_port == ms->dport ) {
#ifdef DEBUG_IP_MASQUERADE_VERBOSE
                                printk(KERN_DEBUG "MASQ: lk/out2 %d %08X:%04hX->%08X:%04hX OK\n",
                                       protocol,
                                       s_addr,
                                       s_port,
                                       d_addr,
                                       d_port);
#endif /* DEBUG_IP_MASQUERADE_VERBOSE */
                                return ms;
                        }
                }
        }
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
#endif /* CONFIG_IP_MASQUERADE_PPTP */

        hash = ip_masq_hash_key(protocol, s_addr, s_port);
        for(ms = ip_masq_s_tab[hash]; ms ; ms = ms->s_link) {
		if (protocol == ms->protocol &&
		    s_addr == ms->saddr && s_port == ms->sport &&
                    d_addr == ms->daddr && d_port == ms->dport ) {
#ifdef DEBUG_IP_MASQUERADE_VERBOSE
			printk("MASQ: lk/out1 %d %08X:%04hX->%08X:%04hX OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port);
#endif
                        return ms;
		}
        }
        hash = ip_masq_hash_key(protocol, s_addr, 0);
        for(ms = ip_masq_s_tab[hash]; ms ; ms = ms->s_link) {
		if (ms->flags & IP_MASQ_F_NO_SPORT &&
		    protocol == ms->protocol &&
		    s_addr == ms->saddr && 
                    d_addr == ms->daddr && d_port == ms->dport ) {
#ifdef DEBUG_IP_MASQUERADE_VERBOSE
			printk("MASQ: lk/out2 %d %08X:%04hX->%08X:%04hX OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port);
#endif
                        return ms;
		}
        }
#ifdef DEBUG_IP_MASQUERADE_VERBOSE
	printk("MASQ: lk/out1 %d %08X:%04hX->%08X:%04hX fail\n",
	       protocol,
	       s_addr,
	       s_port,
	       d_addr,
	       d_port);
#endif
        return NULL;
}

#ifdef CONFIG_IP_MASQUERADE_IPSEC
struct ip_masq *
ip_masq_out_get_ipsec(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port, __u32 o_spi)
{
        unsigned hash;
        struct ip_masq *ms;

	if (protocol != IPPROTO_ESP) {
                return ip_masq_out_get_2(protocol,s_addr,s_port,d_addr,d_port);
        }

        hash = ip_masq_hash_key(protocol, s_addr, s_port);
        for(ms = ip_masq_s_tab[hash]; ms ; ms = ms->s_link) {
 		if (protocol==ms->protocol &&
		    s_addr==ms->saddr &&
		    d_addr==ms->daddr &&
                    o_spi==ms->ospi) {
#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
			printk(KERN_DEBUG "MASQ: IPSEC look/out %08X:%08X->%08X OK\n",
			       s_addr,
			       o_spi,
			       d_addr);
#endif
                        return ms;
		}
        }
        
#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
	printk(KERN_DEBUG "MASQ: IPSEC look/out %08X:%08X->%08X fail\n",
	       s_addr,
	       o_spi,
	       d_addr);
#endif
        return NULL;
}

struct ip_masq *
ip_masq_out_get_isakmp(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port, __u32 cookie)
{
        unsigned hash;
        struct ip_masq *ms;

#ifdef DEBUG_IP_MASQUERADE_IPSEC
	printk(KERN_DEBUG "ip_masq_out_get_isakmp(): ");
	printk("%s -> ", in_ntoa(s_addr));
	printk("%s cookie %lX\n", in_ntoa(d_addr), ntohl(cookie));
#endif /* DEBUG_IP_MASQUERADE_IPSEC */

        if (cookie == 0) {
                printk(KERN_INFO "ip_masq_out_get_isakmp(): ");
                printk("zero cookie from %s\n", in_ntoa(s_addr));
        }

        hash = ip_masq_hash_key(protocol, s_addr, s_port);
        for(ms = ip_masq_s_tab[hash]; ms ; ms = ms->s_link) {
		if (protocol == ms->protocol &&
		    cookie == ms->ospi &&
		    s_addr == ms->saddr && s_port == ms->sport &&
                    d_addr == ms->daddr && d_port == ms->dport ) {
#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
			printk(KERN_DEBUG "MASQ: lk/out1 %d %08X:%04hX->%08X:%04hX %08X OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port,
                               cookie);
#endif
                        return ms;
		}
        }

#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
	printk(KERN_DEBUG "MASQ: lk/out1 %d %08X:%04hX->%08X:%04hX %08X fail\n",
	       protocol,
	       s_addr,
	       s_port,
	       d_addr,
	       d_port,
               cookie);
#endif
        return NULL;
}

#endif /* CONFIG_IP_MASQUERADE_IPSEC */

/*
 *	Returns ip_masq for given proto,m_addr,m_port.
 *      called by allocation routine to find an unused m_port.
 */

struct ip_masq *
ip_masq_getbym(int protocol, __u32 m_addr, __u16 m_port)
{
        unsigned hash;
        struct ip_masq *ms;

        hash = ip_masq_hash_key(protocol, m_addr, m_port);
        for(ms = ip_masq_m_tab[hash]; ms ; ms = ms->m_link) {
 		if ( protocol==ms->protocol &&
                    (m_addr==ms->maddr && m_port==ms->mport))
                        return ms;
        }
        return NULL;
}

static void masq_expire(unsigned long data)
{
	struct ip_masq *ms = (struct ip_masq *)data, *ms_data;
	unsigned long flags;

	if (ms->flags & IP_MASQ_F_CONTROL) {
		/* a control channel is about to expire */
		int idx = 0, reprieve = 0;
#ifdef DEBUG_CONFIG_IP_MASQUERADE
		printk("Masquerade control %s %lX:%X about to expire\n",
				masq_proto_name(ms->protocol),
				ntohl(ms->saddr),ntohs(ms->sport));
#endif
		save_flags(flags);
		cli();

		/*
		 * If any other masquerade entry claims that the expiring entry
		 * is its control channel then keep the control entry alive.
		 * Useful for long running data channels with inactive control
		 * links which we don't want to lose, e.g. ftp.
		 * Assumption: loops such as a->b->a or a->a will never occur.
		 */
		for (idx = 0; idx < IP_MASQ_TAB_SIZE && !reprieve; idx++) {
			for (ms_data = ip_masq_m_tab[idx]; ms_data ; ms_data = ms_data->m_link) {
				if (ms_data->control == ms) {
					reprieve = 1;	/* this control connection can live a bit longer */
					ip_masq_set_expire(ms, ip_masq_expire->tcp_timeout);
#ifdef DEBUG_CONFIG_IP_MASQUERADE
					printk("Masquerade control %s %lX:%X expiry reprieved\n",
							masq_proto_name(ms->protocol),
							ntohl(ms->saddr),ntohs(ms->sport));
#endif
					break;
				}
			}
		}
		restore_flags(flags);
		if (reprieve)
			return;
	}

#ifdef DEBUG_CONFIG_IP_MASQUERADE
	printk("Masqueraded %s %lX:%X expired\n",masq_proto_name(ms->protocol),ntohl(ms->saddr),ntohs(ms->sport));
#endif
	
	save_flags(flags);
	cli();

        if (ip_masq_unhash(ms)) {
                ip_masq_free_ports[masq_proto_num(ms->protocol)]++;
                if (ms->protocol != IPPROTO_ICMP)
                             ip_masq_unbind_app(ms);
                kfree_s(ms,sizeof(*ms));
        }

	restore_flags(flags);
}

#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW
void ip_autofw_expire(unsigned long data)
{
	struct ip_autofw * af;
	af=(struct ip_autofw *) data;
	af->flags&=0xFFFF ^ IP_AUTOFW_ACTIVE;
	af->timer.expires=0;
	af->lastcontact=0;
	if (af->flags & IP_AUTOFW_SECURE)
		af->where=0;
}
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */

/*
 * 	Create a new masquerade list entry, also allocate an
 * 	unused mport, keeping the portnumber between the
 * 	given boundaries MASQ_BEGIN and MASQ_END.
 */

struct ip_masq * ip_masq_new_enh(struct device *dev, int proto, __u32 saddr, __u16 sport, __u32 daddr, __u16 dport, unsigned mflags, __u16 matchport)
{
        struct ip_masq *ms, *mst;
        int ports_tried, *free_ports_p;
	unsigned long flags;
        static int n_fails = 0;

        free_ports_p = &ip_masq_free_ports[masq_proto_num(proto)];

        if (*free_ports_p == 0) {
                if (++n_fails < 5)
                        printk("ip_masq_new(proto=%s): no free ports.\n",
                               masq_proto_name(proto));
                return NULL;
        }
        ms = (struct ip_masq *) kmalloc(sizeof(struct ip_masq), GFP_ATOMIC);
        if (ms == NULL) {
                if (++n_fails < 5)
                        printk("ip_masq_new(proto=%s): no memory available.\n",
                               masq_proto_name(proto));
                return NULL;
        }
        memset(ms, 0, sizeof(*ms));
	init_timer(&ms->timer);
	ms->timer.data     = (unsigned long)ms;
	ms->timer.function = masq_expire;
        ms->protocol	   = proto;
        ms->saddr    	   = saddr;
        ms->sport	   = sport;
        ms->daddr	   = daddr;
        ms->dport	   = dport;
        ms->flags	   = mflags;
        ms->app_data	   = NULL;
	ms->control	   = NULL;

        if (proto == IPPROTO_UDP && !matchport)
                ms->flags |= IP_MASQ_F_NO_DADDR;
        
        /* get masq address from rif */
        ms->maddr	   = dev->pa_addr;
        /*
         *	Setup new entry as not replied yet.
         *	This flag will allow masq. addr (ms->maddr)
         *	to follow forwarding interface address.
         */
        ms->flags         |= IP_MASQ_F_NO_REPLY;

        for (ports_tried = 0; 
	     (*free_ports_p && (ports_tried <= (PORT_MASQ_END - PORT_MASQ_BEGIN)));
	     ports_tried++){

#ifdef CONFIG_IP_MASQUERADE_PPTP
#ifndef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
                /* Ignoring PPTP call IDs.
                 * Don't needlessly increase the TCP port pointer.
                 */
                if (proto == IPPROTO_GRE) {
                        ms->mport = 0;
                        mst = NULL;
                } else {
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
#endif /* CONFIG_IP_MASQUERADE_PPTP */

#ifdef CONFIG_IP_MASQUERADE_IPSEC
                /* ESP masq keys off the SPI, not the port number.
                 * Don't needlessly increase the TCP port pointer.
                 */
                if (proto == IPPROTO_ESP) {
                        ms->mport = 0;
                        mst = NULL;
                } else {
                if (proto == IPPROTO_UDP && ntohs(sport) == UDP_PORT_ISAKMP && ntohs(dport) == UDP_PORT_ISAKMP) {
                        /* the port number cannot be changed */
                        ms->mport = htons(UDP_PORT_ISAKMP);
                        mst = NULL;
                } else {
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

                save_flags(flags);
                cli();
                
		/*
                 *	Try the next available port number
                 */
                if (!matchport || ports_tried)
			ms->mport = htons(masq_port++);
		else
			ms->mport = matchport;
			
		if (masq_port==PORT_MASQ_END) masq_port = PORT_MASQ_BEGIN;
                
                restore_flags(flags);
                
                /*
                 *	lookup to find out if this port is used.
                 */
                
                mst = ip_masq_getbym(proto, ms->maddr, ms->mport);

#ifdef CONFIG_IP_MASQUERADE_PPTP
#ifndef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
                }
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
#endif /* CONFIG_IP_MASQUERADE_PPTP */

#ifdef CONFIG_IP_MASQUERADE_IPSEC
                }
                }
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

                if (mst == NULL || matchport) {
                        save_flags(flags);
                        cli();
                
                        if (*free_ports_p == 0) {
                                restore_flags(flags);
                                break;
                        }
                        (*free_ports_p)--;
                        ip_masq_hash(ms);
                        
                        restore_flags(flags);
                        
                        if (proto != IPPROTO_ICMP)
                              ip_masq_bind_app(ms);
                        n_fails = 0;
                        return ms;
                }
        }
        
        if (++n_fails < 5)
                printk("ip_masq_new(proto=%s): could not get free masq entry (free=%d).\n",
                       masq_proto_name(ms->protocol), *free_ports_p);
        kfree_s(ms, sizeof(*ms));
        return NULL;
}

struct ip_masq * ip_masq_new(struct device *dev, int proto, __u32 saddr, __u16 sport, __u32 daddr, __u16 dport, unsigned mflags)
{
	return (ip_masq_new_enh(dev, proto, saddr, sport, daddr, dport, mflags, 0) );
}

/*
 * 	Set masq expiration (deletion) and adds timer,
 *	if timeout==0 cancel expiration.
 *	Warning: it does not check/delete previous timer!
 */

void ip_masq_set_expire(struct ip_masq *ms, unsigned long tout)
{
	/* There Can Be Only One (timer on a masq table entry, that is) */
        del_timer(&ms->timer);
        if (tout) {
                ms->timer.expires = jiffies+tout;
                add_timer(&ms->timer);
        }
}

static void recalc_check(struct udphdr *uh, __u32 saddr,
	__u32 daddr, int len)
{
	uh->check=0;
	uh->check=csum_tcpudp_magic(saddr,daddr,len,
		IPPROTO_UDP, csum_partial((char *)uh,len,0));
	if(uh->check==0)
		uh->check=0xFFFF;
}
	

#ifdef CONFIG_IP_MASQUERADE_PPTP
/*
 *      Masquerade of GRE connections
 *      to support a PPTP VPN client or server.
 */

/*
 *	Handle outbound GRE packets.
 *
 *	This is largely a copy of ip_fw_masquerade()
 */

int ip_fw_masq_gre(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb   = *skb_p;
        struct iphdr	*iph   = skb->h.iph;
        struct pptp_gre_header	*greh;
#ifdef DEBUG_IP_MASQUERADE_PPTP
#ifdef DEBUG_IP_MASQUERADE_PPTP_VERBOSE
        __u8		*greraw;
#endif /* DEBUG_IP_MASQUERADE_PPTP_VERBOSE */
#endif /* DEBUG_IP_MASQUERADE_PPTP */
        struct ip_masq	*ms;
        unsigned long    flags;


        greh = (struct pptp_gre_header *)&(((char *)iph)[iph->ihl*4]);

#ifdef DEBUG_IP_MASQUERADE_PPTP

        printk(KERN_DEBUG "ip_fw_masq_gre(): ");
        printk("Outbound GRE packet from %s", in_ntoa(iph->saddr));
        printk(" to %s\n", in_ntoa(iph->daddr));

#ifdef DEBUG_IP_MASQUERADE_PPTP_VERBOSE
	greraw = (__u8 *) greh;
	printk(KERN_DEBUG "ip_fw_masq_gre(): ");
	printk("GRE raw: %X %X %X %X %X %X %X %X %X %X %X %X.\n",
		greraw[0],
		greraw[1],
		greraw[2],
		greraw[3],
		greraw[4],
		greraw[5],
		greraw[6],
		greraw[7],
		greraw[8],
		greraw[9],
		greraw[10],
		greraw[11]);
	printk(KERN_DEBUG "ip_fw_masq_gre(): ");
	printk("GRE C: %d R: %d K: %d S: %d s: %d recur: %X.\n",
                greh->has_cksum,
                greh->has_routing,
                greh->has_key,
                greh->has_seq,
                greh->is_strict,
                greh->recur);
	printk(KERN_DEBUG "ip_fw_masq_gre(): ");
	printk("GRE flags: %X ver: %X.\n", greh->flags, greh->version);
	printk(KERN_DEBUG "ip_fw_masq_gre(): ");
	printk("GRE proto: %X.\n", ntohs(greh->protocol));
#endif /* DEBUG_IP_MASQUERADE_PPTP_VERBOSE */
#endif /* DEBUG_IP_MASQUERADE_PPTP */

	if (ntohs(greh->protocol) != 0x880B) {
#ifdef DEBUG_IP_MASQUERADE_PPTP
	  printk(KERN_INFO "ip_fw_masq_gre(): ");
	  printk("GRE protocol %X not 0x880B (non-PPTP encap?) - discarding.\n", ntohs(greh->protocol));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
	  return -1;
	}

	/*
	 *	Look for masq table entry
	 */

        ms = ip_masq_out_get_2(IPPROTO_GRE,
                iph->saddr, 0,
                iph->daddr, 0);

	if (ms!=NULL) {
                /* delete the expiration timer */
        	ip_masq_set_expire(ms,0);

		/*
                 *      Make sure that the masq IP address is correct
                 *      for dynamic IP...
		 */
		if ( (ms->maddr != dev->pa_addr) && (sysctl_ip_dynaddr & 3) ) {
                        printk(KERN_INFO "ip_fw_masq_gre(): ");
                        printk("change maddr from %s", in_ntoa(ms->maddr));
                        printk(" to %s\n", in_ntoa(dev->pa_addr));
		        save_flags(flags);
		        cli();
		        ip_masq_unhash(ms);
		        ms->maddr = dev->pa_addr;
		        ip_masq_hash(ms);
		        restore_flags(flags);
                }
	} else {
                /*
                 *	Nope, not found, create a new entry for it, maybe
                 */
	
#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
                /* masq table entry has to come from control channel sniffing.
                 * If we can't find one, it may have expired.
                 * How can this happen with the control channel active?
                 */
	        printk(KERN_INFO "ip_fw_masq_gre(): ");
                printk("Outbound GRE to %s has no masq table entry.\n",
                        in_ntoa(iph->daddr));
                return -1;
#else /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
                /* call IDs ignored, can create masq table entries on the fly. */
		ms = ip_masq_new(dev, iph->protocol,
				 iph->saddr, 0,
				 iph->daddr, 0,
				 0);

                if (ms == NULL) {
                        printk(KERN_NOTICE "ip_fw_masq_gre(): Couldn't create masq table entry.\n");
			return -1;
                }
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
 	}

        /*
         *	Set iph source addr from ip_masq obj.
         */
 	iph->saddr = ms->maddr;

 	/*
 	 *	set timeout and check IP header
 	 */
 	
        ip_masq_set_expire(ms, MASQUERADE_EXPIRE_PPTP);
 	ip_send_check(iph);

#ifdef DEBUG_IP_MASQUERADE_PPTP
 	printk(KERN_DEBUG "MASQ: GRE O-routed from %s over %s\n",
                in_ntoa(ms->maddr), dev->name);
#endif /* DEBUG_IP_MASQUERADE_PPTP */

	return 0;
}

/*
 *	Handle inbound GRE packets.
 *
 */

int ip_fw_demasq_gre(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb   = *skb_p;
 	struct iphdr	*iph   = skb->h.iph;
 	struct pptp_gre_header	*greh;
#ifdef DEBUG_IP_MASQUERADE_PPTP
#ifdef DEBUG_IP_MASQUERADE_PPTP_VERBOSE
	__u8		*greraw;
#endif /* DEBUG_IP_MASQUERADE_PPTP_VERBOSE */
#endif /* DEBUG_IP_MASQUERADE_PPTP */
        struct ip_masq	*ms;


	greh = (struct pptp_gre_header *)&(((char *)iph)[iph->ihl*4]);

#ifdef DEBUG_IP_MASQUERADE_PPTP

	printk(KERN_DEBUG "ip_fw_demasq_gre(): ");
	printk("Inbound GRE packet from %s", in_ntoa(iph->saddr));
	printk(" to %s\n", in_ntoa(iph->daddr));

#ifdef DEBUG_IP_MASQUERADE_PPTP_VERBOSE
	greraw = (__u8 *) greh;
	printk(KERN_DEBUG "ip_fw_demasq_gre(): ");
	printk("GRE raw: %X %X %X %X %X %X %X %X %X %X %X %X.\n",
		greraw[0],
		greraw[1],
		greraw[2],
		greraw[3],
		greraw[4],
		greraw[5],
		greraw[6],
		greraw[7],
		greraw[8],
		greraw[9],
		greraw[10],
		greraw[11]);
	printk(KERN_DEBUG "ip_fw_demasq_gre(): ");
	printk("GRE C: %d R: %d K: %d S: %d s: %d recur: %X.\n",
                greh->has_cksum,
                greh->has_routing,
                greh->has_key,
                greh->has_seq,
                greh->is_strict,
                greh->recur);
	printk(KERN_DEBUG "ip_fw_demasq_gre(): ");
	printk("GRE flags: %X ver: %X.\n", greh->flags, greh->version);
	printk(KERN_DEBUG "ip_fw_demasq_gre(): ");
	printk("GRE proto: %X.\n", ntohs(greh->protocol));
#endif /* DEBUG_IP_MASQUERADE_PPTP_VERBOSE */

#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
	printk(KERN_DEBUG "ip_fw_demasq_gre(): ");
	printk("PPTP call ID: %X.\n", ntohs(greh->call_id));
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */

#endif /* DEBUG_IP_MASQUERADE_PPTP */

	if (ntohs(greh->protocol) != 0x880B) {
#ifdef DEBUG_IP_MASQUERADE_PPTP
	  printk(KERN_INFO "ip_fw_demasq_gre(): ");
	  printk("GRE protocol %X not 0x880B (non-PPTP encap?) - discarding.\n", ntohs(greh->protocol));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
	  return -1;
	}

 	/*
 	 *      Look for a masq table entry and reroute if found
         */

#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
        ms = ip_masq_getbym(IPPROTO_GRE,
                iph->daddr, greh->call_id);
#else /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
        ms = ip_masq_in_get_2(IPPROTO_GRE,
                iph->saddr, 0,
                iph->daddr, 0);
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */

        if (ms != NULL)
        {
                /* delete the expiration timer */
		ip_masq_set_expire(ms,0);

                iph->daddr = ms->saddr;

#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
                /*
                 * change peer call ID to original value
                 * (saved in masq table source port)
                 */

                greh->call_id = ms->sport;

#ifdef DEBUG_IP_MASQUERADE_PPTP
	        printk(KERN_DEBUG "ip_fw_demasq_gre(): ");
                printk("inbound PPTP from %s call ID now %X\n",
                       in_ntoa(iph->saddr), ntohs(greh->call_id));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */

                /*
                 * resum checksums and set timeout
                 */
		ip_masq_set_expire(ms, MASQUERADE_EXPIRE_PPTP);
                ip_send_check(iph);

#ifdef DEBUG_IP_MASQUERADE_PPTP
                printk(KERN_DEBUG "MASQ: GRE I-routed to %s\n", in_ntoa(iph->daddr));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                return 1;
 	}

 	/* sorry, all this trouble for a no-hit :) */
	printk(KERN_INFO "ip_fw_demasq_gre(): ");
	printk("Inbound from %s has no masq table entry.\n", in_ntoa(iph->saddr));
 	return 0;
}

#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
/*
 *      Define all of the PPTP control channel message structures.
 *      Sniff the control channel looking for start- and end-call
 *      messages, and masquerade the Call ID as if it was a TCP
 *      port.
 */

#define PPTP_CONTROL_PACKET            1
#define PPTP_MGMT_PACKET               2
#define PPTP_MAGIC_COOKIE              0x1A2B3C4D

struct PptpPacketHeader {
       __u16 packetLength;
       __u16 packetType;
       __u32 magicCookie;
};

/* PptpControlMessageType values */
#define PPTP_START_SESSION_REQUEST     1
#define PPTP_START_SESSION_REPLY       2
#define PPTP_STOP_SESSION_REQUEST      3
#define PPTP_STOP_SESSION_REPLY        4
#define PPTP_ECHO_REQUEST              5
#define PPTP_ECHO_REPLY                6
#define PPTP_OUT_CALL_REQUEST          7
#define PPTP_OUT_CALL_REPLY            8
#define PPTP_IN_CALL_REQUEST           9
#define PPTP_IN_CALL_REPLY             10
#define PPTP_CALL_CLEAR_REQUEST        11
#define PPTP_CALL_DISCONNECT_NOTIFY    12
#define PPTP_CALL_ERROR_NOTIFY         13
#define PPTP_WAN_ERROR_NOTIFY          14
#define PPTP_SET_LINK_INFO             15

struct PptpControlHeader {
    __u16 messageType;
    __u16 reserved;
};

struct PptpOutCallRequest {
    __u16 callID;
    __u16 callSerialNumber;
    __u32 minBPS;
    __u32 maxBPS;
    __u32 bearerType;
    __u32 framingType;
    __u16 packetWindow;
    __u16 packetProcDelay;
    __u16 reserved1;
    __u16 phoneNumberLength;
    __u16 reserved2;
    __u8  phoneNumber[64];
    __u8  subAddress[64];
};

struct PptpOutCallReply {
    __u16 callID;
    __u16 peersCallID;
    __u8  resultCode;
    __u8  generalErrorCode;
    __u16 causeCode;
    __u32 connectSpeed;
    __u16 packetWindow;
    __u16 packetProcDelay;
    __u32 physChannelID;
};

struct PptpInCallRequest {
    __u16 callID;
    __u16 callSerialNumber;
    __u32 callBearerType;
    __u32 physChannelID;
    __u16 dialedNumberLength;
    __u16 dialingNumberLength;
    __u8  dialedNumber[64];
    __u8  dialingNumber[64];
    __u8  subAddress[64];
};

struct PptpInCallReply {
    __u16 callID;
    __u16 peersCallID;
    __u8  resultCode;
    __u8  generalErrorCode;
    __u16 packetWindow;
    __u16 packetProcDelay;
    __u16 reserved;
};

struct PptpCallDisconnectNotify {
    __u16 callID;
    __u8  resultCode;
    __u8  generalErrorCode;
    __u16 causeCode;
    __u16 reserved;
    __u8  callStatistics[128];
};

struct PptpWanErrorNotify {
    __u16 peersCallID;
    __u16 reserved;
    __u32 crcErrors;
    __u32 framingErrors;
    __u32 hardwareOverRuns;
    __u32 bufferOverRuns;
    __u32 timeoutErrors;
    __u32 alignmentErrors;
};

struct PptpSetLinkInfo {
    __u16 peersCallID;
    __u16 reserved;
    __u32 sendAccm;
    __u32 recvAccm;
};


/* Packet sent to or from PPTP control port. Process it. */
/* Yes, all of this should be in a kernel module. Real Soon Now... */
void ip_masq_pptp(struct sk_buff *skb, struct ip_masq *ms, struct device *dev)
{
        struct iphdr    *iph   = skb->h.iph;
        struct PptpPacketHeader  *pptph = NULL;
        struct PptpControlHeader *ctlh = NULL;
        union {
                char *req;
                struct PptpOutCallRequest       *ocreq;
                struct PptpOutCallReply         *ocack;
                struct PptpInCallRequest        *icreq;
                struct PptpInCallReply          *icack;
                struct PptpCallDisconnectNotify *disc;
                struct PptpWanErrorNotify       *wanerr;
                struct PptpSetLinkInfo          *setlink;
        } pptpReq;
        struct ip_masq  *ms_gre = NULL;

        /*
         * The GRE data channel will be treated as the "control channel"
         * for the purposes of masq because there are keepalives happening
         * on the control channel, whereas the data channel may be subject
         * to relatively long periods of inactivity.
         */
         
        pptph = (struct PptpPacketHeader *)&(((char *)iph)[sizeof(struct iphdr) + sizeof(struct tcphdr)]);
#ifdef DEBUG_IP_MASQUERADE_PPTP_VERBOSE
        printk(KERN_DEBUG "ip_masq_pptp(): ");
        printk("LEN=%d TY=%d MC=%lX", ntohs(pptph->packetLength),
                ntohs(pptph->packetType), ntohl(pptph->magicCookie));
	printk(" from %s", in_ntoa(iph->saddr));
	printk(" to %s\n", in_ntoa(iph->daddr));
#endif /* DEBUG_IP_MASQUERADE_PPTP_VERBOSE */

        if (ntohs(pptph->packetType) == PPTP_CONTROL_PACKET &&
            ntohl(pptph->magicCookie) == PPTP_MAGIC_COOKIE) {
#ifdef DEBUG_IP_MASQUERADE_PPTP
                printk(KERN_DEBUG "ip_masq_pptp(): ");
                printk("PPTP control packet from %s", in_ntoa(iph->saddr));
                printk(" to %s\n", in_ntoa(iph->daddr));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                ctlh = (struct PptpControlHeader *)&(((char*)pptph)[sizeof(struct PptpPacketHeader)]);
                pptpReq.req = &(((char*)ctlh)[sizeof(struct PptpControlHeader)]);
#ifdef DEBUG_IP_MASQUERADE_PPTP_VERBOSE
                printk(KERN_DEBUG "ip_masq_pptp(): ");
                printk("MTY=%X R0=%X\n",
                        ntohs(ctlh->messageType), ctlh->reserved);
#endif /* DEBUG_IP_MASQUERADE_PPTP_VERBOSE */

                switch (ntohs(ctlh->messageType))
                {
                        case PPTP_OUT_CALL_REQUEST:
                                if (iph->daddr == ms->daddr)    /* outbound only */
                                {
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                        printk(KERN_DEBUG "ip_masq_pptp(): ");
                                        printk("Call request, call ID %X\n",
                                                ntohs(pptpReq.ocreq->callID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        ms_gre = ip_masq_new(dev, IPPROTO_GRE,
                                                ms->saddr, pptpReq.ocreq->callID,
                                                ms->daddr, 0,
                                                0);
                                        if (ms_gre != NULL)
                                        {
                                                ms->control = ms_gre;
                                                ms_gre->flags |= IP_MASQ_F_CONTROL;
                                                ip_masq_set_expire(ms_gre, 0);
                                                ip_masq_set_expire(ms_gre, 2*60*HZ);
                                                pptpReq.ocreq->callID = ms_gre->mport;
                                                printk(KERN_INFO "ip_masq_pptp(): ");
                                                printk("Req outcall PPTP sess %s", in_ntoa(ms->saddr));
                                                printk(" -> %s", in_ntoa(ms->daddr));
                                                printk(" Call ID %X -> %X.\n", ntohs(ms_gre->sport), ntohs(ms_gre->mport));
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                                printk(KERN_DEBUG "ip_masq_pptp(): ");
                                                printk("masqed call ID %X\n",
                                                        ntohs(pptpReq.ocreq->callID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        } else {
                                                printk(KERN_NOTICE "ip_masq_pptp(): ");
                                                printk("Couldn't create GRE masq table entry (%s)\n", "OUT_CALL_REQ");
                                        }
                                }
                        break;
                        case PPTP_OUT_CALL_REPLY:
                                if (iph->saddr == ms->daddr)    /* inbound (masqueraded client) */
                                {
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                        printk(KERN_DEBUG "ip_masq_pptp(): ");
                                        printk("Call reply, peer call ID %X\n",
                                                ntohs(pptpReq.ocack->peersCallID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        ms_gre = ip_masq_getbym(IPPROTO_GRE,
                                                ms->maddr, pptpReq.ocack->peersCallID);
                                        if (ms_gre != NULL)
                                        {
                                                ip_masq_set_expire(ms_gre, 0);
                                                ip_masq_set_expire(ms_gre, 2*60*HZ);
                                                pptpReq.ocack->peersCallID = ms_gre->sport;
                                                printk(KERN_INFO "ip_masq_pptp(): ");
                                                printk("Estab outcall PPTP sess %s", in_ntoa(ms->saddr));
                                                printk(" -> %s", in_ntoa(ms->daddr));
                                                printk(" Call ID %X -> %X.\n", ntohs(ms_gre->sport), ntohs(ms_gre->mport));
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                                printk(KERN_DEBUG "ip_masq_pptp(): ");
                                                printk("unmasqed call ID %X\n",
                                                        ntohs(pptpReq.ocack->callID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        } else {
                                                printk(KERN_INFO "ip_masq_pptp(): ");
                                                printk("Lost GRE masq table entry (%s)\n", "OUT_CALL_REPLY");
                                        }
                                }
                        break;
                        case PPTP_IN_CALL_REQUEST:
                                if (iph->daddr == ms->daddr)    /* outbound only */
                                {
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                        printk(KERN_DEBUG "ip_masq_pptp(): ");
                                        printk("Call request, call ID %X\n",
                                                ntohs(pptpReq.icreq->callID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        ms_gre = ip_masq_new(dev, IPPROTO_GRE,
                                                 ms->saddr, pptpReq.icreq->callID,
                                                 ms->daddr, 0,
                                                 0);
                                        if (ms_gre != NULL)
                                        {
                                                ms->control = ms_gre;
                                                ms_gre->flags |= IP_MASQ_F_CONTROL;
                                                ip_masq_set_expire(ms_gre, 0);
                                                ip_masq_set_expire(ms_gre, 2*60*HZ);
                                                pptpReq.icreq->callID = ms_gre->mport;
                                                printk(KERN_INFO "ip_masq_pptp(): ");
                                                printk("Req incall PPTP sess %s", in_ntoa(ms->saddr));
                                                printk(" -> %s", in_ntoa(ms->daddr));
                                                printk(" Call ID %X -> %X.\n", ntohs(ms_gre->sport), ntohs(ms_gre->mport));
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                                printk(KERN_DEBUG "ip_masq_pptp(): ");
                                                printk("masqed call ID %X\n",
                                                        ntohs(pptpReq.icreq->callID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        } else {
                                                printk(KERN_NOTICE "ip_masq_pptp(): ");
                                                printk("Couldn't create GRE masq table entry (%s)\n", "IN_CALL_REQ");
                                        }
                                }
                        break;
                        case PPTP_IN_CALL_REPLY:
                                if (iph->saddr == ms->daddr)    /* inbound (masqueraded client) */
                                {
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                        printk(KERN_DEBUG "ip_masq_pptp(): ");
                                        printk("Call reply, peer call ID %X\n",
                                                ntohs(pptpReq.icack->peersCallID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        ms_gre = ip_masq_getbym(IPPROTO_GRE,
                                                ms->maddr, pptpReq.icack->peersCallID);
                                        if (ms_gre != NULL)
                                        {
                                                ip_masq_set_expire(ms_gre, 0);
                                                ip_masq_set_expire(ms_gre, 2*60*HZ);
                                                pptpReq.icack->peersCallID = ms_gre->sport;
                                                printk(KERN_INFO "ip_masq_pptp(): ");
                                                printk("Estab incall PPTP sess %s", in_ntoa(ms->saddr));
                                                printk(" -> %s", in_ntoa(ms->daddr));
                                                printk(" Call ID %X -> %X.\n", ntohs(ms_gre->sport), ntohs(ms_gre->mport));
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                                printk(KERN_DEBUG "ip_masq_pptp(): ");
                                                printk("unmasqed call ID %X\n",
                                                        ntohs(pptpReq.icack->callID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        } else {
                                                printk(KERN_INFO "ip_masq_pptp(): ");
                                                printk("Lost GRE masq table entry (%s)\n", "IN_CALL_REPLY");
                                        }
                                }
                        break;
                        case PPTP_CALL_DISCONNECT_NOTIFY:
                                if (iph->daddr == ms->daddr)    /* outbound only */
                                {
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                        printk(KERN_DEBUG "ip_masq_pptp(): ");
                                        printk("Disconnect notify, call ID %X\n",
                                                ntohs(pptpReq.disc->callID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        ms_gre = ip_masq_out_get_2(IPPROTO_GRE,
                                                iph->saddr, pptpReq.disc->callID,
                                                iph->daddr, 0);
                                        if (ms_gre != NULL)
                                        {
                                                /*
                                                 * expire the data channel
                                                 * table entry quickly now.
                                                 */
                                                ip_masq_set_expire(ms_gre, 0);
                                                ip_masq_set_expire(ms_gre, 30*HZ);
                                                ms->control = NULL;
                                                ms_gre->flags &= ~IP_MASQ_F_CONTROL;
                                                pptpReq.disc->callID = ms_gre->mport;
                                                printk(KERN_INFO "ip_masq_pptp(): ");
                                                printk("Disconnect PPTP sess %s", in_ntoa(ms->saddr));
                                                printk(" -> %s", in_ntoa(ms->daddr));
                                                printk(" Call ID %X -> %X.\n", ntohs(ms_gre->sport), ntohs(ms_gre->mport));
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                                printk(KERN_DEBUG "ip_masq_pptp(): ");
                                                printk("masqed call ID %X\n",
                                                        ntohs(pptpReq.disc->callID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        }
                                }
                        break;
                        case PPTP_WAN_ERROR_NOTIFY:
                                if (iph->saddr == ms->daddr)    /* inbound only */
                                {
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                        printk(KERN_DEBUG "ip_masq_pptp(): ");
                                        printk("Error notify, peer call ID %X\n",
                                                ntohs(pptpReq.wanerr->peersCallID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        ms_gre = ip_masq_getbym(IPPROTO_GRE,
                                                ms->maddr, pptpReq.wanerr->peersCallID);
                                        if (ms_gre != NULL)
                                        {
                                                pptpReq.wanerr->peersCallID = ms_gre->sport;
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                                printk(KERN_DEBUG "ip_masq_pptp(): ");
                                                printk("unmasqed call ID %X\n",
                                                        ntohs(pptpReq.wanerr->peersCallID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        } else {
                                                printk(KERN_INFO "ip_masq_pptp(): ");
                                                printk("Lost GRE masq table entry (%s)\n", "WAN_ERROR_NOTIFY");
                                        }
                                }
                        break;
                        case PPTP_SET_LINK_INFO:
                                if (iph->saddr == ms->daddr)    /* inbound only */
                                {
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                        printk(KERN_DEBUG "ip_masq_pptp(): ");
                                        printk("Set link info, peer call ID %X\n",
                                                ntohs(pptpReq.setlink->peersCallID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        ms_gre = ip_masq_getbym(IPPROTO_GRE,
                                                ms->maddr, pptpReq.setlink->peersCallID);
                                        if (ms_gre != NULL)
                                        {
                                                pptpReq.setlink->peersCallID = ms_gre->sport;
#ifdef DEBUG_IP_MASQUERADE_PPTP
                                                printk(KERN_DEBUG "ip_masq_pptp(): ");
                                                printk("unmasqed call ID %X\n",
                                                        ntohs(pptpReq.setlink->peersCallID));
#endif /* DEBUG_IP_MASQUERADE_PPTP */
                                        } else {
                                                printk(KERN_INFO "ip_masq_pptp(): ");
                                                printk("Lost GRE masq table entry (%s)\n", "SET_LINK_INFO");
                                        }
                                }
                        break;
                }
        }
}
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */

static struct symbol_table pptp_masq_syms = {
#include <linux/symtab_begin.h>
	X(ip_fw_masq_gre),
	X(ip_fw_demasq_gre),
#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
	X(ip_masq_pptp),
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
#include <linux/symtab_end.h>
};

#endif /* CONFIG_IP_MASQUERADE_PPTP */


#ifdef CONFIG_IP_MASQUERADE_IPSEC
/*
 *      Quick-and-dirty handling of ESP connections
 *      John Hardin <jhardin@wolfenet.com> gets all blame...
 */

/*
 *	Handle outbound ESP packets.
 *
 *	This is largely a copy of ip_fw_masquerade()
 *
 * To associate inbound traffic with outbound traffic, we only
 * allow one session per remote host to be negotiated at a time.
 * If a packet comes in and there's no masq table entry for it,
 * then check for other masq table entries for the same server
 * with the inbound SPI set to zero (i.e. no response yet). If
 * found, discard the packet.
 * This will DoS the server for the duration of the connection
 * attempt, so keep the masq entry's lifetime short until a
 * response comes in.
 * If multiple masqueraded hosts are in contention for the same
 * remote host, enforce round-robin access. This may lead to
 * misassociation of response traffic if the response is delayed
 * a great deal, but the masqueraded hosts will clean that up
 * if it happens.
 */

int ip_fw_masq_esp(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb   = *skb_p;
        struct iphdr	*iph   = skb->h.iph;
        struct ip_masq	*ms;
        unsigned long    flags;
        __u32 o_spi;
        __u16 fake_sport;
        unsigned long    timeout = MASQUERADE_EXPIRE_IPSEC;

        o_spi = *((__u32 *)&(((char *)iph)[iph->ihl*4]));
        fake_sport = (__u16) ntohl(o_spi) & 0xffff;

#ifdef DEBUG_IP_MASQUERADE_IPSEC
        printk(KERN_DEBUG "ip_fw_masq_esp(): ");
        printk("pkt %s", in_ntoa(iph->saddr));
        printk(" -> %s SPI %lX (fakeport %X)\n", in_ntoa(iph->daddr), ntohl(o_spi), fake_sport);
#endif /* DEBUG_IP_MASQUERADE_IPSEC */

        if (o_spi == 0) {
                /* illegal SPI - discard */
                printk(KERN_INFO "ip_fw_masq_esp(): ");
                printk("zero SPI from %s discarded\n", in_ntoa(iph->saddr));
                return -1;
        }

	/*
	 *	Look for masq table entry
	 */

        ms = ip_masq_out_get_ipsec(IPPROTO_ESP,
                iph->saddr, fake_sport,
                iph->daddr, 0,
                o_spi);

	if (ms!=NULL) {
                if (ms->ispi == IPSEC_INIT_SQUELCHED) {
                        /* squelched: toss the packet without changing the timer */
#ifdef DEBUG_IP_MASQUERADE_IPSEC
                        printk(KERN_INFO "ip_fw_masq_esp(): ");
			printk("init %s ", in_ntoa(iph->saddr));
			printk("-> %s SPI %lX ", in_ntoa(iph->daddr), ntohl(o_spi));
			printk("squelched\n");
#endif /* DEBUG_IP_MASQUERADE_IPSEC */
                        return -1;
                }

                /* delete the expiration timer */
        	ip_masq_set_expire(ms,0);

		/*
                 *      Make sure that the masq IP address is correct
                 *      for dynamic IP...
		 */
		if ( (ms->maddr != dev->pa_addr) && (sysctl_ip_dynaddr & 3) ) {
                        printk(KERN_INFO "ip_fw_masq_esp(): ");
                        printk("change maddr from %s", in_ntoa(ms->maddr));
                        printk(" to %s\n", in_ntoa(dev->pa_addr));
		        save_flags(flags);
		        cli();
		        ip_masq_unhash(ms);
		        ms->maddr = dev->pa_addr;
		        ip_masq_hash(ms);
		        restore_flags(flags);
                }

                if (ms->ispi == 0) {
                        /* no response yet, keep timeout short */
			timeout = MASQUERADE_EXPIRE_IPSEC_INIT;
                        if (ms->blocking) {
                                /* prevent DoS: limit init packet timer resets */
                                ms->ocnt++;
        #ifdef DEBUG_IP_MASQUERADE_IPSEC
                                printk(KERN_INFO "ip_fw_masq_esp(): ");
                                printk("init %s ", in_ntoa(iph->saddr));
                                printk("-> %s SPI %lX ", in_ntoa(iph->daddr), ntohl(o_spi));
                                printk("retry %d\n", ms->ocnt);
        #endif /* DEBUG_IP_MASQUERADE_IPSEC */
                                if (ms->ocnt > IPSEC_INIT_RETRIES) {
                                        /* more than IPSEC_INIT_RETRIES tries, give up */
                                        printk(KERN_INFO "ip_fw_masq_esp(): ");
                                        printk("init %s ", in_ntoa(iph->saddr));
                                        printk("-> %s SPI %lX ", in_ntoa(iph->daddr), ntohl(o_spi));
                                        printk("no response after %d tries, unblocking & squelching\n", ms->ocnt);
                                        /* squelch that source+SPI for a bit */
                                        timeout = 30*HZ;
                                        save_flags(flags);
                                        cli();
                                        ip_masq_unhash(ms);
                                        ms->ispi = IPSEC_INIT_SQUELCHED;
                                        ms->dport = IPSEC_INIT_SQUELCHED;
                                        ip_masq_hash(ms);
                                        restore_flags(flags);
                                        ip_masq_set_expire(ms, timeout);
                                        /* toss the packet */
                                        return -1;
                                }
                        }
                }
	} else {
                /*
                 *	Nope, not found, create a new entry for it, maybe
                 */
	
                /* see if there are any pending inits with the same destination... */
                ms = ip_masq_in_get_ipsec(IPPROTO_ESP,
                        iph->daddr, 0,
                        0, 0,
                        0);
                
                if (ms != NULL) {
                        /* found one with ispi == 0 */
                        if (ms->saddr != iph->saddr) {
                                /* it's not ours, don't step on their toes */
                                printk(KERN_INFO "ip_fw_masq_esp(): ");
                                printk("init %s ", in_ntoa(iph->saddr));
                                printk("-> %s ", in_ntoa(iph->daddr));
                                printk("temporarily blocked by pending ");
                                printk("%s init\n", in_ntoa(ms->saddr));
                                /* let it know it has competition */
                                ms->blocking = 1;
                                /* toss the packet */
                                return -1;
                        }
                        if (ms->ospi != o_spi) {
                                /* SPIs differ, still waiting for a previous attempt to expire */
                                printk(KERN_INFO "ip_fw_masq_esp(): ");
                                printk("init %s ", in_ntoa(iph->saddr));
                                printk("-> %s SPI %lX ", in_ntoa(iph->daddr), ntohl(o_spi));
                                printk("temporarily blocked by pending ");
                                printk("init w/ SPI %lX\n", ntohl(ms->ospi));
                                /* let it know it has competition */
                                ms->blocking = 1;
                                /* toss the packet */
                                return -1;
                        }
                } else  /* nothing pending, make new entry, pending response */
                        ms = ip_masq_new(dev, iph->protocol,
				 iph->saddr, fake_sport,
				 iph->daddr, 0,
				 0);

                if (ms == NULL) {
                        printk(KERN_NOTICE "ip_fw_masq_esp(): Couldn't create masq table entry.\n");
			return -1;
                }

                ms->blocking = ms->ocnt = 0;
                ms->ospi = o_spi;
                timeout = MASQUERADE_EXPIRE_IPSEC_INIT;      /* fairly brief timeout while waiting for a response */
 	}

        /*
         *	Set iph source addr from ip_masq obj.
         */
 	iph->saddr = ms->maddr;

 	/*
 	 *	set timeout and check IP header
 	 */
 	
        ip_masq_set_expire(ms, timeout);
 	ip_send_check(iph);

#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
 	printk(KERN_DEBUG "MASQ: ESP O-routed from %s over %s\n",
                in_ntoa(ms->maddr), dev->name);
#endif /* DEBUG_IP_MASQUERADE_IPSEC_VERBOSE */

	return 0;
}

/*
 *	Handle inbound ESP packets.
 *
 */

int ip_fw_demasq_esp(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb   = *skb_p;
 	struct iphdr	*iph   = skb->h.iph;
        struct ip_masq	*ms;
        unsigned long   flags;
        __u32 i_spi;
        __u16 fake_sport;
#ifndef CONFIG_IP_MASQUERADE_IPSEC_NOGUESS
	#define ESP_GUESS_SZ 5			/* minimum 3, please */
	#define ESP_CAND_MIN_TM 5*60*HZ		/* max 10*60*HZ? */
	unsigned	hash;
	int		i, ii,
			ncand = 0, nguess = 0;
	__u16		isakmp;
	__u32		cand_ip,
			guess_ip[ESP_GUESS_SZ];
	unsigned long	cand_tm,
			guess_tm[ESP_GUESS_SZ];
	struct sk_buff 	*skb_cl;
	struct iphdr	*iph_cl;
#endif /* CONFIG_IP_MASQUERADE_IPSEC_NOGUESS */

        i_spi = *((__u32 *)&(((char *)iph)[iph->ihl*4]));
        fake_sport = (__u16) ntohl(i_spi) & 0xffff;

#ifdef DEBUG_IP_MASQUERADE_IPSEC
        printk(KERN_DEBUG "ip_fw_demasq_esp(): ");
	printk("pkt %s", in_ntoa(iph->saddr));
	printk(" -> %s SPI %lX (fakeport %X)\n", in_ntoa(iph->daddr), ntohl(i_spi), fake_sport);
#endif /* DEBUG_IP_MASQUERADE_IPSEC */

        if (i_spi == 0) {
                /* illegal SPI - discard */
                printk(KERN_INFO "ip_fw_demasq_esp(): ");
                printk("zero SPI from %s discarded\n", in_ntoa(iph->saddr));
                return -1;
        }

        if (i_spi == IPSEC_INIT_SQUELCHED) {
                /* Ack! This shouldn't happen! */
		/* IPSEC_INIT_SQUELCHED is chosen to be a reserved value as of 4/99 */
                printk(KERN_NOTICE "ip_fw_demasq_esp(): ");
                printk("SPI from %s is IPSEC_INIT_SQUELCHED - modify ip_masq.c!\n", in_ntoa(iph->saddr));
                return -1;
        }

 	/*
 	 *      Look for a masq table entry and reroute if found
         */

        ms = ip_masq_in_get_ipsec(IPPROTO_ESP,
                iph->saddr, fake_sport,
                iph->daddr, 0,
                i_spi);

        if (ms != NULL)
        {
                /* delete the expiration timer */
		ip_masq_set_expire(ms,0);

                iph->daddr = ms->saddr;

                if (ms->ispi == 0) {
#ifdef DEBUG_IP_MASQUERADE_IPSEC
                        printk(KERN_INFO "ip_fw_demasq_esp(): ");
                        printk("resp from %s SPI %lX", in_ntoa(iph->saddr), ntohl(i_spi));
                        printk(" routed to %s (SPI %lX)\n", in_ntoa(ms->saddr), ntohl(ms->ospi));
#endif /* DEBUG_IP_MASQUERADE_IPSEC */
		        save_flags(flags);
		        cli();
		        ip_masq_unhash(ms);
		        ms->ispi = i_spi;
		        ms->dport = fake_sport;
		        ip_masq_hash(ms);
		        restore_flags(flags);
                }
                
                /*
                 * resum checksums and set timeout
                 */
		ip_masq_set_expire(ms, MASQUERADE_EXPIRE_IPSEC);
                ip_send_check(iph);

#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
                printk(KERN_DEBUG "MASQ: ESP I-routed to %s\n", in_ntoa(iph->daddr));
#endif /* DEBUG_IP_MASQUERADE_IPSEC_VERBOSE */
                return 1;
 	}

#ifndef CONFIG_IP_MASQUERADE_IPSEC_NOGUESS
	/* Guess who this packet is likely intended for:
	 * Scan the UDP masq table for local hosts that have communicated via
	 * ISAKMP with the host who sent this packet.
	 * Using an insertion sort with duplicate IP suppression, build a list
	 * of the ESP_GUESS_SZ most recent ISAKMP sessions (determined by
	 * sorting in decreasing order of timeout timer).
	 * Clone the original packet and send it to those hosts, but DON'T make
	 * a masq table entry, as we're only guessing. It is assumed that the correct
	 * host will respond to the traffic and that will create a masq table entry.
	 * To limit the list a bit, don't consider any ISAKMP masq entries with
	 * less than ESP_CAND_MIN_TM time to live. This should be some value less
	 * than the IPSEC table timeout or *all* entries will be ignored...
	 */

#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
	printk(KERN_DEBUG "ip_fw_demasq_esp(): ");
        printk("guessing from %s SPI %lX\n", in_ntoa(iph->saddr), ntohl(i_spi));
#endif /* DEBUG_IP_MASQUERADE_IPSEC_VERBOSE */

	/* zero out the guess table */
	for (i = 0;i < ESP_GUESS_SZ; i++) {
		guess_ip[i] = 0;
		guess_tm[i] = 0;
	}

	/* scan ISAKMP sessions with the source host */
	isakmp = htons(UDP_PORT_ISAKMP);
        hash = ip_masq_hash_key(IPPROTO_UDP, iph->saddr, isakmp);
	for(ms = ip_masq_d_tab[hash]; ms ; ms = ms->d_link) {
		if (ms->protocol == IPPROTO_UDP &&
		    ms->daddr == iph->saddr &&
		    ms->sport == isakmp &&
		    ms->dport == isakmp &&
		    ms->mport == isakmp &&
		    ms->ospi != 0) {
			/* a candidate... */
			ncand++;
			cand_ip = ms->saddr;
			cand_tm = ms->timer.expires - jiffies;
#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
			printk(KERN_DEBUG "ip_fw_demasq_esp(): ");
			printk("cand %d: IP %s TM %ld\n", ncand, in_ntoa(cand_ip), cand_tm);
#endif /* DEBUG_IP_MASQUERADE_IPSEC_VERBOSE */
			if (cand_tm > ESP_CAND_MIN_TM) {
				/* traffic is recent enough, add to list (maybe) */
				for (i = 0; i < ESP_GUESS_SZ; i++) {
					if (cand_tm > guess_tm[i]) {
						/* newer */
						if (guess_ip[i] != 0 && cand_ip != guess_ip[i]) {
							/* newer and IP different - insert */
							if (i < (ESP_GUESS_SZ - 1)) {
								/* move entries down the list,
								 * find first entry after this slot
								 * where the IP is 0 (unused) or
								 * IP == candidate (older traffic, same host)
								 * rather than simply going to the end of the list,
								 * for efficiency (don't shift zeros) and
								 * duplicate IP suppression (don't keep older entries
								 * having the same IP)
								 */
								for (ii = i + 1; ii < (ESP_GUESS_SZ - 1); ii++) {
									if (guess_ip[ii] == 0 || guess_ip[ii] == cand_ip)
										break;
								}
								for (ii-- ; ii >= i; ii--) {
									guess_ip[ii+1] = guess_ip[ii];
									guess_tm[ii+1] = guess_tm[ii];
								}
							}
						}
						guess_ip[i] = cand_ip;
						guess_tm[i] = cand_tm;
						break;
					}
					if (cand_ip == guess_ip[i]) {
						/* fresher entry already there */
						break;
					}
				}
			}
		}
	}
	
	if (guess_ip[0]) {
		/* had guesses - send */
		if (guess_ip[1]) {
			/* multiple guesses, send a copy to all */
			for (i = 0; guess_ip[i] != 0; i++) {
				nguess++;
#ifdef DEBUG_IP_MASQUERADE_IPSEC_VERBOSE
				printk(KERN_DEBUG "ip_fw_demasq_esp(): ");
				printk("guess %d: IP %s TM %ld\n", nguess, in_ntoa(guess_ip[i]), guess_tm[i]);
#endif /* DEBUG_IP_MASQUERADE_IPSEC_VERBOSE */
				/* duplicate and send the skb */
				if ((skb_cl = skb_copy(skb, GFP_ATOMIC)) == NULL) {
					printk(KERN_INFO "ip_fw_demasq_esp(): ");
					printk("guessing: cannot copy skb\n");
				} else {
					iph_cl = skb_cl->h.iph;
					iph_cl->daddr = guess_ip[i];
					ip_send_check(iph_cl);
					ip_forward(skb_cl, dev, IPFWD_MASQUERADED, iph_cl->daddr);
					kfree_skb(skb_cl, FREE_WRITE);
				}
			}
#ifdef DEBUG_IP_MASQUERADE_IPSEC
                        printk(KERN_INFO "ip_fw_demasq_esp(): ");
                        printk("guessing from %s SPI %lX sent to", in_ntoa(iph->saddr), ntohl(i_spi));
                        printk(" %d hosts (%d cand)\n", nguess, ncand);
#endif /* DEBUG_IP_MASQUERADE_IPSEC */
			return -1;	/* discard original packet */
		} else {
			/* only one guess, send original packet to that host */
			iph->daddr = guess_ip[0];
			ip_send_check(iph);

#ifdef DEBUG_IP_MASQUERADE_IPSEC
                        printk(KERN_INFO "ip_fw_demasq_esp(): ");
                        printk("guessing from %s SPI %lX sent to", in_ntoa(iph->saddr), ntohl(i_spi));
                        printk(" %s (%d cand)\n", in_ntoa(guess_ip[0]), ncand);
#endif /* DEBUG_IP_MASQUERADE_IPSEC */
			return 1;
		}
	}
#endif /* CONFIG_IP_MASQUERADE_IPSEC_NOGUESS */

 	/* sorry, all this trouble for a no-hit :) */
        printk(KERN_INFO "ip_fw_demasq_esp(): ");
	printk("Inbound from %s SPI %lX has no masq table entry.\n", in_ntoa(iph->saddr), ntohl(i_spi));
 	return 0;
}

static struct symbol_table ipsec_masq_syms = {
#include <linux/symtab_begin.h>
	X(ip_masq_out_get_ipsec),
	X(ip_masq_in_get_ipsec),
	X(ip_masq_out_get_isakmp),
	X(ip_masq_in_get_isakmp),
	X(ip_fw_masq_esp),
	X(ip_fw_demasq_esp),
#include <linux/symtab_end.h>
};

#endif /* CONFIG_IP_MASQUERADE_IPSEC */


int ip_fw_masquerade(struct sk_buff **skb_ptr, struct device *dev)
{
	struct sk_buff  *skb=*skb_ptr;
	struct iphdr	*iph = skb->h.iph;
	__u16	*portptr;
	struct ip_masq	*ms;
	int		size;
        unsigned long 	timeout;

	/*
	 * We can only masquerade protocols with ports...
	 * [TODO]
	 * We may need to consider masq-ing some ICMP related to masq-ed protocols
	 */

        if (iph->protocol==IPPROTO_ICMP) 
            return (ip_fw_masq_icmp(skb_ptr,dev));
#ifdef CONFIG_IP_MASQUERADE_PPTP
        if (iph->protocol==IPPROTO_GRE) 
            return (ip_fw_masq_gre(skb_ptr,dev));
#endif /* CONFIG_IP_MASQUERADE_PPTP */
#ifdef CONFIG_IP_MASQUERADE_IPSEC
        if (iph->protocol==IPPROTO_ESP) 
            return (ip_fw_masq_esp(skb_ptr,dev));
#endif /* CONFIG_IP_MASQUERADE_IPSEC */
	if (iph->protocol!=IPPROTO_UDP && iph->protocol!=IPPROTO_TCP)
		return -1;

	/*
	 *	Now hunt the list to see if we have an old entry
	 */

	portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
#ifdef DEBUG_CONFIG_IP_MASQUERADE
	printk("Outgoing %s %lX:%X -> %lX:%X\n",
		masq_proto_name(iph->protocol),
		ntohl(iph->saddr), ntohs(portptr[0]),
		ntohl(iph->daddr), ntohs(portptr[1]));
#endif

        ms = ip_masq_out_get(iph);

	if (ms!=NULL) {
                ip_masq_set_expire(ms,0);
                
                /*
                 *	If sysctl & 3 and either
		 *        no pkt has been received yet
		 *      or
		 *        sysctl & 4
                 *	in this tunnel ...
                 *	 "You are welcome, diald, ipppd, pppd-3.3...".
                 */
                if ( (sysctl_ip_dynaddr & 3) && (ms->flags & IP_MASQ_F_NO_REPLY || sysctl_ip_dynaddr & 4) && dev->pa_addr != ms->maddr) {
                        unsigned long flags;
                        if (sysctl_ip_dynaddr & 2) {
                                printk(KERN_INFO "ip_fw_masquerade(): change maddr from %s",
                                       in_ntoa(ms->maddr));
                                printk(" to %s\n", in_ntoa(dev->pa_addr));
                        }
                        save_flags(flags);
                        cli();
                        ip_masq_unhash(ms);
                        ms->maddr = dev->pa_addr;
                        ip_masq_hash(ms);
                        restore_flags(flags);
                }
                
		/*
		 *      Set sport if not defined yet (e.g. ftp PASV).  Because
		 *	masq entries are hashed on sport, unhash with old value
		 *	and hash with new.
		 */

		if ( ms->flags & IP_MASQ_F_NO_SPORT && ms->protocol == IPPROTO_TCP ) {
			unsigned long flags;
			ms->flags &= ~IP_MASQ_F_NO_SPORT;
			save_flags(flags);
			cli();
			ip_masq_unhash(ms);
			ms->sport = portptr[0];
			ip_masq_hash(ms);	/* hash on new sport */
			restore_flags(flags);
#ifdef DEBUG_CONFIG_IP_MASQUERADE
			printk("ip_fw_masquerade(): filled sport=%d\n",
			       ntohs(ms->sport));
#endif
		}
	}

#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW
	/* update any ipautofw entries .. */
	ip_autofw_update_out(iph->saddr, iph->daddr, portptr[1], 
			     iph->protocol);
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */

	/*
	 *	Nope, not found, create a new entry for it
	 */
	if (ms==NULL)
	{
#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW
		/* if the source port is supposed to match the masq port, then
		   make it so */
		if (ip_autofw_check_direct(portptr[1],iph->protocol))
	                ms = ip_masq_new_enh(dev, iph->protocol,
        	                         iph->saddr, portptr[0],
                	                 iph->daddr, portptr[1],
                        	         0,
                        	         portptr[0]);
                else
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */
	                ms = ip_masq_new_enh(dev, iph->protocol,
        	                         iph->saddr, portptr[0],
                	                 iph->daddr, portptr[1],
                        	         0,
                        	         0);
                if (ms == NULL)
			return -1;

#ifdef CONFIG_IP_MASQUERADE_IPSEC
                if (iph->protocol == IPPROTO_UDP && ntohs(portptr[0]) == UDP_PORT_ISAKMP && ntohs(portptr[1]) == UDP_PORT_ISAKMP) {
                        /* save the initiator cookie */
                        ms->ospi = *((__u32 *)&portptr[4]);
                }
#endif /* CONFIG_IP_MASQUERADE_IPSEC */
 	}

#ifdef CONFIG_IP_MASQUERADE_PPTP
#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
	if (iph->protocol == IPPROTO_TCP && ntohs(portptr[1]) == PPTP_CONTROL_PORT)
	{
                /*
                 * Packet sent to PPTP control port. Process it.
                 * May change call ID word in request, but
                 * packet length will not change.
                 */
		ip_masq_pptp(skb, ms, dev);
	}
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
#endif /* CONFIG_IP_MASQUERADE_PPTP */

 	/*
 	 *	Change the fragments origin
 	 */
 	
 	size = skb->len - ((unsigned char *)portptr - skb->h.raw);
        /*
         *	Set iph addr and port from ip_masq obj.
         */
 	iph->saddr = ms->maddr;
 	portptr[0] = ms->mport;

 	/*
 	 *	Attempt ip_masq_app call.
         *	will fix ip_masq and iph seq stuff
 	 */
        if (ip_masq_app_pkt_out(ms, skb_ptr, dev) != 0)
	{
                /*
                 *	skb has possibly changed, update pointers.
                 */
                skb = *skb_ptr;
                iph = skb->h.iph;
                portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
                size = skb->len - ((unsigned char *)portptr-skb->h.raw);
        }

 	/*
 	 *	Adjust packet accordingly to protocol
 	 */
 	
 	if (masq_proto_num(iph->protocol)==0)
 	{
#ifdef CONFIG_IP_MASQUERADE_IPSEC
		if (iph->protocol == IPPROTO_UDP && ntohs(portptr[0]) == UDP_PORT_ISAKMP && ntohs(portptr[1]) == UDP_PORT_ISAKMP) {
			/* ISAKMP timeout should be same as ESP timeout to allow for rekeying */
			timeout = MASQUERADE_EXPIRE_IPSEC;
		} else
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

                timeout = ip_masq_expire->udp_timeout;
 		recalc_check((struct udphdr *)portptr,iph->saddr,iph->daddr,size);
 	}
 	else
 	{
 		struct tcphdr *th;
 		th = (struct tcphdr *)portptr;

		/* Set the flags up correctly... */
		if (th->fin)
		{
			ms->flags |= IP_MASQ_F_SAW_FIN_OUT;
		}

		if (th->rst)
		{
			ms->flags |= IP_MASQ_F_SAW_RST;
		}

 		/*
 		 *	Timeout depends if FIN packet has been seen
		 *	Very short timeout if RST packet seen.
 		 */
 		if (ms->flags & IP_MASQ_F_SAW_RST)
		{
                        timeout = 1;
		}
 		else if ((ms->flags & IP_MASQ_F_SAW_FIN) == IP_MASQ_F_SAW_FIN)
		{
                        timeout = ip_masq_expire->tcp_fin_timeout;
		}
 		else timeout = ip_masq_expire->tcp_timeout;

		skb->csum = csum_partial((void *)(th + 1), size - sizeof(*th), 0);
 		tcp_send_check(th,iph->saddr,iph->daddr,size,skb);
 	}
        ip_masq_set_expire(ms, timeout);
 	ip_send_check(iph);

 #ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("O-routed from %lX:%X over %s\n",ntohl(ms->maddr),ntohs(ms->mport),dev->name);
 #endif

	return 0;
 }


/*
 *	Handle ICMP messages in forward direction.
 *	Find any that might be relevant, check against existing connections,
 *	forward to masqueraded host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded
 */

int ip_fw_masq_icmp(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb   = *skb_p;
 	struct iphdr	*iph   = skb->h.iph;
	struct icmphdr  *icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
	struct iphdr    *ciph;	/* The ip header contained within the ICMP */
	__u16	        *pptr;	/* port numbers from TCP/UDP contained header */
	struct ip_masq	*ms;
	unsigned short   len   = ntohs(iph->tot_len) - (iph->ihl * 4);

#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
 	printk("Incoming forward ICMP (%d,%d) %lX -> %lX\n",
	        icmph->type, ntohs(icmp_id(icmph)),
 		ntohl(iph->saddr), ntohl(iph->daddr));
#endif

#ifdef CONFIG_IP_MASQUERADE_ICMP		
	if ((icmph->type == ICMP_ECHO ) ||
	    (icmph->type == ICMP_TIMESTAMP ) ||
	    (icmph->type == ICMP_INFO_REQUEST ) ||
	    (icmph->type == ICMP_ADDRESS )) {
#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
		printk("MASQ: icmp request rcv %lX->%lX id %d type %d\n",
		       ntohl(iph->saddr),
		       ntohl(iph->daddr),
		       ntohs(icmp_id(icmph)),
		       icmph->type);
#endif
		ms = ip_masq_out_get_2(iph->protocol,
				       iph->saddr,
				       icmp_id(icmph),
				       iph->daddr,
				       icmp_hv_req(icmph));
		if (ms == NULL) {
			ms = ip_masq_new(dev,
					 iph->protocol,
					 iph->saddr,
					 icmp_id(icmph),
					 iph->daddr,
					 icmp_hv_req(icmph),
					 0);
			if (ms == NULL)
				return (-1);
#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
			printk("MASQ: Create new icmp entry\n");
#endif	              
		}
		ip_masq_set_expire(ms, 0);
		/* Rewrite source address */
                
                /*
                 *	If sysctl & 3 and either
		 *        no pkt has been received yet
		 *      or
		 *        sysctl & 4
                 *	in this tunnel ...
                 *	 "You are welcome, diald, ipppd, pppd-3.3...".
                 */
                if ( (sysctl_ip_dynaddr & 3) && (ms->flags & IP_MASQ_F_NO_REPLY || sysctl_ip_dynaddr & 4) && dev->pa_addr != ms->maddr) {
                        unsigned long flags;
#ifdef DEBUG_CONFIG_IP_MASQUERADE
                        printk(KERN_INFO "ip_fw_masq_icmp(): change masq.addr %s",
                               in_ntoa(ms->maddr));
                        printk("-> %s\n", in_ntoa(dev->pa_addr));
#endif
                        save_flags(flags);
                        cli();
                        ip_masq_unhash(ms);
                        ms->maddr = dev->pa_addr;
                        ip_masq_hash(ms);
                        restore_flags(flags);
                }
                
		iph->saddr = ms->maddr;
		ip_send_check(iph);
		/* Rewrite port (id) */
		(icmph->un).echo.id = ms->mport;
		icmph->checksum = 0;
		icmph->checksum = ip_compute_csum((unsigned char *)icmph, len);
		ip_masq_set_expire(ms, MASQUERADE_EXPIRE_ICMP);
#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
		printk("MASQ: icmp request rwt %lX->%lX id %d type %d\n",
		       ntohl(iph->saddr),
		       ntohl(iph->daddr),
		       ntohs(icmp_id(icmph)),
		       icmph->type);
#endif
		return (1);
	}
#endif

	/* 
	 * Work through seeing if this is for us.
	 * These checks are supposed to be in an order that
	 * means easy things are checked first to speed up
	 * processing.... however this means that some
	 * packets will manage to get a long way down this
	 * stack and then be rejected, but thats life
	 */
	if ((icmph->type != ICMP_DEST_UNREACH) &&
	    (icmph->type != ICMP_SOURCE_QUENCH) &&
	    (icmph->type != ICMP_TIME_EXCEEDED))
		return 0;

	/* Now find the contained IP header */
	ciph = (struct iphdr *) (icmph + 1);

#ifdef CONFIG_IP_MASQUERADE_ICMP
	if (ciph->protocol == IPPROTO_ICMP) {
		/*
		 * This section handles ICMP errors for ICMP packets
		 */
		struct icmphdr  *cicmph = (struct icmphdr *)((char *)ciph + 
							     (ciph->ihl<<2));

#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
		printk("MASQ: fw icmp/icmp rcv %lX->%lX id %d type %d\n",
		       ntohl(ciph->saddr),
		       ntohl(ciph->daddr),
		       ntohs(icmp_id(cicmph)),
		       cicmph->type);
#endif
		ms = ip_masq_out_get_2(ciph->protocol, 
				      ciph->daddr,
				      icmp_id(cicmph),
				      ciph->saddr,
				      icmp_hv_rep(cicmph));

		if (ms == NULL)
			return 0;

		/* Now we do real damage to this packet...! */
		/* First change the source IP address, and recalc checksum */
		iph->saddr = ms->maddr;
		ip_send_check(iph);
	
		/* Now change the *dest* address in the contained IP */
		ciph->daddr = ms->maddr;
		ip_send_check(ciph);

		/* Change the ID to the masqed one! */
		(cicmph->un).echo.id = ms->mport;
	
		/* And finally the ICMP checksum */
		icmph->checksum = 0;
		icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);

#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
		printk("MASQ: fw icmp/icmp rwt %lX->%lX id %d type %d\n",
		       ntohl(ciph->saddr),
		       ntohl(ciph->daddr),
		       ntohs(icmp_id(cicmph)),
		       cicmph->type);
#endif
		return 1;
	}
#endif /* CONFIG_IP_MASQUERADE_ICMP */

	/* We are only interested ICMPs generated from TCP or UDP packets */
	if ((ciph->protocol != IPPROTO_UDP) && (ciph->protocol != IPPROTO_TCP))
		return 0;

	/* 
	 * Find the ports involved - this packet was 
	 * incoming so the ports are right way round
	 * (but reversed relative to outer IP header!)
	 */
	pptr = (__u16 *)&(((char *)ciph)[ciph->ihl*4]);

	/* Ensure the checksum is correct */
	if (ip_compute_csum((unsigned char *) icmph, len)) 
	{
		/* Failed checksum! */
		printk(KERN_DEBUG "MASQ: forward ICMP: failed checksum from %s!\n", 
		       in_ntoa(iph->saddr));
		return(-1);
	}

#ifdef DEBUG_CONFIG_IP_MASQUERADE
	printk("Handling forward ICMP for %lX:%X -> %lX:%X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));
#endif

	/* This is pretty much what ip_masq_out_get() does */
	ms = ip_masq_out_get_2(ciph->protocol, 
			       ciph->daddr, 
			       pptr[1], 
			       ciph->saddr, 
			       pptr[0]);

	if (ms == NULL)
		return 0;

	/* Now we do real damage to this packet...! */
	/* First change the source IP address, and recalc checksum */
	iph->saddr = ms->maddr;
	ip_send_check(iph);
	
	/* Now change the *dest* address in the contained IP */
	ciph->daddr = ms->maddr;
	ip_send_check(ciph);
	
	/* the TCP/UDP dest port - cannot redo check */
	pptr[1] = ms->mport;

	/* And finally the ICMP checksum */
	icmph->checksum = 0;
	icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);

#ifdef DEBUG_CONFIG_IP_MASQUERADE
	printk("Rewrote forward ICMP to %lX:%X -> %lX:%X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));
#endif

	return 1;
}

/*
 *	Handle ICMP messages in reverse (demasquerade) direction.
 *	Find any that might be relevant, check against existing connections,
 *	forward to masqueraded host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded
 */

int ip_fw_demasq_icmp(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb   = *skb_p;
 	struct iphdr	*iph   = skb->h.iph;
	struct icmphdr  *icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
	struct iphdr    *ciph;	/* The ip header contained within the ICMP */
	__u16	        *pptr;	/* port numbers from TCP/UDP contained header */
	struct ip_masq	*ms;
	unsigned short   len   = ntohs(iph->tot_len) - (iph->ihl * 4);

#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
 	printk("MASQ: icmp in/rev (%d,%d) %lX -> %lX\n",
	        icmph->type, ntohs(icmp_id(icmph)),
 		ntohl(iph->saddr), ntohl(iph->daddr));
#endif

#ifdef CONFIG_IP_MASQUERADE_ICMP		
	if ((icmph->type == ICMP_ECHOREPLY) ||
	    (icmph->type == ICMP_TIMESTAMPREPLY) ||
	    (icmph->type == ICMP_INFO_REPLY) ||
	    (icmph->type == ICMP_ADDRESSREPLY))	{
#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
		printk("MASQ: icmp reply rcv %lX->%lX id %d type %d, req %d\n",
		       ntohl(iph->saddr),
		       ntohl(iph->daddr),
		       ntohs(icmp_id(icmph)),
		       icmph->type,
		       icmp_type_request(icmph->type));
#endif
		ms = ip_masq_in_get_2(iph->protocol,
				      iph->saddr,
				      icmp_hv_rep(icmph),
				      iph->daddr,
				      icmp_id(icmph));
		if (ms == NULL)
			return 0;

		ip_masq_set_expire(ms,0);
                
                /*
                 *	got reply, so clear flag
                 */
                ms->flags &= ~IP_MASQ_F_NO_REPLY;

		/* Reset source address */
		iph->daddr = ms->saddr;
		/* Redo IP header checksum */
		ip_send_check(iph);
		/* Set ID to fake port number */
		(icmph->un).echo.id = ms->sport;
		/* Reset ICMP checksum and set expiry */
		icmph->checksum=0;
		icmph->checksum=ip_compute_csum((unsigned char *)icmph,len);
		ip_masq_set_expire(ms, MASQUERADE_EXPIRE_ICMP);
#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
		printk("MASQ: icmp reply rwt %lX->%lX id %d type %d\n",
		       ntohl(iph->saddr),
		       ntohl(iph->daddr),
		       ntohs(icmp_id(icmph)),
		       icmph->type);
#endif
		return 1;
	} else {
#endif
		if ((icmph->type != ICMP_DEST_UNREACH) &&
		    (icmph->type != ICMP_SOURCE_QUENCH) &&
		    (icmph->type != ICMP_TIME_EXCEEDED))
			return 0;
#ifdef CONFIG_IP_MASQUERADE_ICMP
	}
#endif
	/*
	 * If we get here we have an ICMP error of one of the above 3 types
	 * Now find the contained IP header
	 */
	ciph = (struct iphdr *) (icmph + 1);

#ifdef CONFIG_IP_MASQUERADE_ICMP
	if (ciph->protocol == IPPROTO_ICMP) {
		/*
		 * This section handles ICMP errors for ICMP packets
		 *
		 * First get a new ICMP header structure out of the IP packet
		 */
		struct icmphdr  *cicmph = (struct icmphdr *)((char *)ciph + 
							     (ciph->ihl<<2));

#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
		printk("MASQ: rv icmp/icmp rcv %lX->%lX id %d type %d\n",
		       ntohl(ciph->saddr),
		       ntohl(ciph->daddr),
		       ntohs(icmp_id(cicmph)),
		       cicmph->type);
#endif
		ms = ip_masq_in_get_2(ciph->protocol, 
				      ciph->daddr, 
				      icmp_hv_req(cicmph),
				      ciph->saddr, 
				      icmp_id(cicmph));

		if (ms == NULL)
			return 0;

		/* Now we do real damage to this packet...! */
		/* First change the dest IP address, and recalc checksum */
		iph->daddr = ms->saddr;
		ip_send_check(iph);
	
		/* Now change the *source* address in the contained IP */
		ciph->saddr = ms->saddr;
		ip_send_check(ciph);

		/* Change the ID to the original one! */
		(cicmph->un).echo.id = ms->sport;

		/* And finally the ICMP checksum */
		icmph->checksum = 0;
		icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);

#ifdef DEBUG_CONFIG_IP_MASQUERADE_ICMP
		printk("MASQ: rv icmp/icmp rwt %lX->%lX id %d type %d\n",
		       ntohl(ciph->saddr),
		       ntohl(ciph->daddr),
		       ntohs(icmp_id(cicmph)),
		       cicmph->type);
#endif
		return 1;
	}
#endif /* CONFIG_IP_MASQUERADE_ICMP */

	/* We are only interested ICMPs generated from TCP or UDP packets */
	if ((ciph->protocol != IPPROTO_UDP) && 
	    (ciph->protocol != IPPROTO_TCP))
		return 0;

	/* 
	 * Find the ports involved - remember this packet was 
	 * *outgoing* so the ports are reversed (and addresses)
	 */
	pptr = (__u16 *)&(((char *)ciph)[ciph->ihl*4]);
	if (ntohs(pptr[0]) < PORT_MASQ_BEGIN ||
 	    ntohs(pptr[0]) > PORT_MASQ_END)
 		return 0;

	/* Ensure the checksum is correct */
	if (ip_compute_csum((unsigned char *) icmph, len)) 
	{
		/* Failed checksum! */
		printk(KERN_DEBUG "MASQ: reverse ICMP: failed checksum from %s!\n", 
		       in_ntoa(iph->saddr));
		return(-1);
	}

#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Handling reverse ICMP for %lX:%X -> %lX:%X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));
#endif

	/* This is pretty much what ip_masq_in_get() does, except params are wrong way round */
	ms = ip_masq_in_get_2(ciph->protocol,
			      ciph->daddr,
			      pptr[1],
			      ciph->saddr,
			      pptr[0]);

	if (ms == NULL)
		return 0;

	/* Now we do real damage to this packet...! */
	/* First change the dest IP address, and recalc checksum */
	iph->daddr = ms->saddr;
	ip_send_check(iph);
	
	/* Now change the *source* address in the contained IP */
	ciph->saddr = ms->saddr;
	ip_send_check(ciph);
	
	/* the TCP/UDP source port - cannot redo check */
	pptr[0] = ms->sport;

	/* And finally the ICMP checksum */
	icmph->checksum = 0;
	icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);

#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Rewrote reverse ICMP to %lX:%X -> %lX:%X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));
#endif

	return 1;
}


 /*
  *	Check if it's an masqueraded port, look it up,
  *	and send it on its way...
  *
  *	Better not have many hosts using the designated portrange
  *	as 'normal' ports, or you'll be spending many time in
  *	this function.
  */

int ip_fw_demasquerade(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb = *skb_p;
 	struct iphdr	*iph = skb->h.iph;
 	__u16	*portptr;
 	struct ip_masq	*ms;
	unsigned short len;
	unsigned long 	timeout = MASQUERADE_EXPIRE_TCP;
#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW 
 	struct ip_autofw *af;
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */


	switch (iph->protocol) {
	case IPPROTO_ICMP:
		return(ip_fw_demasq_icmp(skb_p, dev));
#ifdef CONFIG_IP_MASQUERADE_PPTP
	case IPPROTO_GRE:
		return(ip_fw_demasq_gre(skb_p, dev));
#endif /* CONFIG_IP_MASQUERADE_PPTP */
#ifdef CONFIG_IP_MASQUERADE_IPSEC
	case IPPROTO_ESP:
		return(ip_fw_demasq_esp(skb_p, dev));
#endif /* CONFIG_IP_MASQUERADE_IPSEC */
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		/* Make sure packet is in the masq range */
		portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
		if ((ntohs(portptr[1]) < PORT_MASQ_BEGIN ||
		     ntohs(portptr[1]) > PORT_MASQ_END)
#ifdef CONFIG_IP_MASQUERADE_IPSEC
                    && ((iph->protocol != IPPROTO_UDP) || (ntohs(portptr[0]) != UDP_PORT_ISAKMP) || (ntohs(portptr[1]) != UDP_PORT_ISAKMP))
#endif /* CONFIG_IP_MASQUERADE_IPSEC */
#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW 
		    && !ip_autofw_check_range(iph->saddr, portptr[1], 
					      iph->protocol, 0)
		    && !ip_autofw_check_direct(portptr[1], iph->protocol)
		    && !ip_autofw_check_port(portptr[1], iph->protocol)
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */
			)
			return 0;

		/* Check that the checksum is OK */
		len = ntohs(iph->tot_len) - (iph->ihl * 4);
		if ((iph->protocol == IPPROTO_UDP) && (portptr[3] == 0))
			/* No UDP checksum */
			break;

		switch (skb->ip_summed) 
		{
			case CHECKSUM_NONE:
				skb->csum = csum_partial((char *)portptr, len, 0);
			case CHECKSUM_HW:
				if (csum_tcpudp_magic(iph->saddr, iph->daddr, len,
						      iph->protocol, skb->csum))
				{
					printk(KERN_DEBUG "MASQ: failed TCP/UDP checksum from %s!\n", 
					       in_ntoa(iph->saddr));
					return -1;
				}
			default:
				/* CHECKSUM_UNNECESSARY */
		}
		break;
	default:
		return 0;
	}


#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Incoming %s %lX:%X -> %lX:%X\n",
 		masq_proto_name(iph->protocol),
 		ntohl(iph->saddr), ntohs(portptr[0]),
 		ntohl(iph->daddr), ntohs(portptr[1]));
#endif
 	/*
 	 * reroute to original host:port if found...
         */

        ms = ip_masq_in_get(iph);

#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW 

        if (ms == NULL && (af=ip_autofw_check_range(iph->saddr, portptr[1], iph->protocol, 0))) 
	{
#ifdef DEBUG_CONFIG_IP_MASQUERADE
		printk("ip_autofw_check_range\n");
#endif
        	ms = ip_masq_new_enh(dev, iph->protocol,
        			     af->where, portptr[1],
        			     iph->saddr, portptr[0],
        			     0,
        			     portptr[1]);
        }
        if ( ms == NULL && (af=ip_autofw_check_port(portptr[1], iph->protocol)) ) 
	{
#ifdef DEBUG_CONFIG_IP_MASQUERADE
		printk("ip_autofw_check_port\n");
#endif
        	ms = ip_masq_new_enh(dev, iph->protocol,
        			     af->where, htons(af->hidden),
        			     iph->saddr, portptr[0],
        			     IP_MASQ_F_AFW_PORT,
        			     htons(af->visible));
        }
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */

        if (ms != NULL)
        {
#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW 
        	ip_autofw_update_in(iph->saddr, portptr[1], iph->protocol);
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */
        	
		/* Stop the timer ticking.... */
		ip_masq_set_expire(ms,0);

                /*
                 *	got reply, so clear flag
                 */
                ms->flags &= ~IP_MASQ_F_NO_REPLY;
                
                /*
                 *	Set dport if not defined yet.
                 */

                if ( ms->flags & IP_MASQ_F_NO_DPORT && ms->protocol == IPPROTO_TCP ) {
                        ms->flags &= ~IP_MASQ_F_NO_DPORT;
                        ms->dport = portptr[0];
#ifdef DEBUG_CONFIG_IP_MASQUERADE
                        printk("ip_fw_demasquerade(): filled dport=%d\n",
                               ntohs(ms->dport));
#endif
                }
                if (ms->flags & IP_MASQ_F_NO_DADDR && ms->protocol == IPPROTO_TCP)  {
                        ms->flags &= ~IP_MASQ_F_NO_DADDR;
                        ms->daddr = iph->saddr;
#ifdef DEBUG_CONFIG_IP_MASQUERADE
                        printk("ip_fw_demasquerade(): filled daddr=%X\n",
                               ntohs(ms->daddr));
#endif
                }
                iph->daddr = ms->saddr;
                portptr[1] = ms->sport;

                /*
                 *	Attempt ip_masq_app call.
                 *	will fix ip_masq and iph ack_seq stuff
                 */

                if (ip_masq_app_pkt_in(ms, skb_p, dev) != 0)
                {
                        /*
                         *	skb has changed, update pointers.
                         */

                        skb = *skb_p;
                        iph = skb->h.iph;
                        portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
                        len = ntohs(iph->tot_len) - (iph->ihl * 4);
                }

#ifdef CONFIG_IP_MASQUERADE_PPTP
#ifdef CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT
                if (iph->protocol == IPPROTO_TCP && ntohs(portptr[0]) == PPTP_CONTROL_PORT)
                {
                        /*
                         * Packet received from PPTP control port. Process it.
                         * May change call ID word in request, but
                         * packet length will not change.
                         */
                        ip_masq_pptp(skb, ms, dev);
                }
#endif /* CONFIG_IP_MASQUERADE_PPTP_MULTICLIENT */
#endif /* CONFIG_IP_MASQUERADE_PPTP */

                /*
                 * Yug! adjust UDP/TCP and IP checksums, also update
		 * timeouts.
		 * If a TCP RST is seen collapse the tunnel (by using short timeout)!
                 */
                if (masq_proto_num(iph->protocol)==0)
		{
                        recalc_check((struct udphdr *)portptr,iph->saddr,iph->daddr,len);

#ifdef CONFIG_IP_MASQUERADE_IPSEC
			if (iph->protocol == IPPROTO_UDP && ntohs(portptr[0]) == UDP_PORT_ISAKMP && ntohs(portptr[1]) == UDP_PORT_ISAKMP) {
				/* ISAKMP timeout should be same as ESP timeout to allow for rekeying */
				timeout = MASQUERADE_EXPIRE_IPSEC;
			} else
#endif /* CONFIG_IP_MASQUERADE_IPSEC */

			timeout = ip_masq_expire->udp_timeout;
		}
                else
                {
			struct tcphdr *th;
			if(len>=sizeof(struct tcphdr))
			{
	                        skb->csum = csum_partial((void *)(((struct tcphdr *)portptr) + 1),
                                                 len - sizeof(struct tcphdr), 0);
                        	tcp_send_check((struct tcphdr *)portptr,iph->saddr,iph->daddr,len,skb);

				/* Check if TCP FIN or RST */
				th = (struct tcphdr *)portptr;
				if (th->fin)
				{
					ms->flags |= IP_MASQ_F_SAW_FIN_IN;
				}
				if (th->rst)
				{
					ms->flags |= IP_MASQ_F_SAW_RST;
				}
			
				/* Now set the timeouts */
				if (ms->flags & IP_MASQ_F_SAW_RST)
				{
					timeout = 1;
				}
				else if ((ms->flags & IP_MASQ_F_SAW_FIN) == IP_MASQ_F_SAW_FIN)
				{
					timeout = ip_masq_expire->tcp_fin_timeout;
				}
				else timeout = ip_masq_expire->tcp_timeout;
			}
                }	
		ip_masq_set_expire(ms, timeout);
                ip_send_check(iph);
#ifdef DEBUG_CONFIG_IP_MASQUERADE
                printk("I-routed to %lX:%X\n",ntohl(iph->daddr),ntohs(portptr[1]));
#endif
                return 1;
 	}

 	/* sorry, all this trouble for a no-hit :) */
 	return 0;
}

/*
 *	/proc/net entries
 */


#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW

static int ip_autofw_procinfo(char *buffer, char **start, off_t offset,
			      int length, int unused)
{
	off_t pos=0, begin=0;
	struct ip_autofw * af;
	int len=0;
	
	len=sprintf(buffer,"Type Prot Low  High Vis  Hid  Where    Last     CPto CPrt Timer Flags\n"); 
        
        for(af = ip_autofw_hosts; af ; af = af->next)
	{
		len+=sprintf(buffer+len,"%4X %4X %04X-%04X/%04X %04X %08lX %08lX %04X %04X %6lu %4X\n",
					af->type,
					af->protocol,
					af->low,
					af->high,
					af->visible,
					af->hidden,
					ntohl(af->where),
					ntohl(af->lastcontact),
					af->ctlproto,
					af->ctlport,
					(af->timer.expires<jiffies ? 0 : af->timer.expires-jiffies), 
					af->flags);

		pos=begin+len;
		if(pos<offset) 
		{
 			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
        }
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	return len;
}
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */

static int ip_msqhst_procinfo(char *buffer, char **start, off_t offset,
			      int length, int unused)
{
	off_t pos=0, begin;
	struct ip_masq *ms;
	unsigned long flags;
	char temp[129];
        int idx = 0;
	int len=0;
	
	if (offset < 128) 
	{
#ifdef CONFIG_IP_MASQUERADE_ICMP
		sprintf(temp,
			"Prc FromIP   FPrt ToIP     TPrt Masq Init-seq  Delta PDelta Expires (free=%d,%d,%d)",
			ip_masq_free_ports[0], ip_masq_free_ports[1], ip_masq_free_ports[2]); 
#else /* !defined(CONFIG_IP_MASQUERADE_ICMP) */
		sprintf(temp,
			"Prc FromIP   FPrt ToIP     TPrt Masq Init-seq  Delta PDelta Expires (free=%d,%d)",
			ip_masq_free_ports[0], ip_masq_free_ports[1]); 
#endif /* CONFIG_IP_MASQUERADE_ICMP */
		len = sprintf(buffer, "%-127s\n", temp);
	}
	pos = 128;
	save_flags(flags);
	cli();
        
        for(idx = 0; idx < IP_MASQ_TAB_SIZE; idx++)
        for(ms = ip_masq_m_tab[idx]; ms ; ms = ms->m_link)
	{
		int timer_active;
		pos += 128;
		if (pos <= offset)
			continue;

		timer_active = del_timer(&ms->timer);
		if (!timer_active)
			ms->timer.expires = jiffies;
		sprintf(temp,"%s %08lX:%04X %08lX:%04X %04X %08X %6d %6d %7lu",
			masq_proto_name(ms->protocol),
			ntohl(ms->saddr), ntohs(ms->sport),
			ntohl(ms->daddr), ntohs(ms->dport),
			ntohs(ms->mport),
			ms->out_seq.init_seq,
			ms->out_seq.delta,
			ms->out_seq.previous_delta,
			ms->timer.expires-jiffies);
		if (timer_active)
			add_timer(&ms->timer);
		len += sprintf(buffer+len, "%-127s\n", temp);

		if(len >= length)
			goto done;
        }
done:
	restore_flags(flags);
	begin = len - (pos - offset);
	*start = buffer + begin;
	len -= begin;
	if(len>length)
		len = length;
	return len;
}

/*
 *	Initialize ip masquerading
 */
int ip_masq_init(void)
{
        register_symtab (&ip_masq_syms);
#ifdef CONFIG_IP_MASQUERADE_PPTP
        register_symtab (&pptp_masq_syms);
#endif /* CONFIG_IP_MASQUERADE_PPTP */
#ifdef CONFIG_IP_MASQUERADE_IPSEC
        register_symtab (&ipsec_masq_syms);
#endif /* CONFIG_IP_MASQUERADE_IPSEC */
#ifdef CONFIG_PROC_FS        
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPMSQHST, 13, "ip_masquerade",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_msqhst_procinfo
	});
#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPAUTOFW, 9, "ip_autofw",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_autofw_procinfo
	});
#endif /* CONFIG_IP_MASQUERADE_IPAUTOFW */
#endif	
        ip_masq_app_init();

        return 0;
}
