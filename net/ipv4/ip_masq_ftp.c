/*
 *		IP_MASQ_FTP ftp masquerading module
 *
 *
 * Version:	@(#)ip_masq_ftp.c 0.10   20/09/00
 *
 * Author:	Wouter Gadeyne
 *		
 *
 * Fixes:
 *	Wouter Gadeyne		:	Fixed masquerading support of ftp PORT commands
 * 	Juan Jose Ciarlante	:	Code moved and adapted from ip_fw.c
 * 	Keith Owens		:	Add keep alive for ftp control channel
 *	Nigel Metheringham	:	Added multiple port support
 * 	Juan Jose Ciarlante	:	Use control_add() for ftp control chan
 * 	Juan Jose Ciarlante	:	Litl bits for 2.1
 *	Juan Jose Ciarlante	:	use ip_masq_listen() 
 *	Juan Jose Ciarlante	: 	use private app_data for own flag(s)
 *  Bjarni R. Einarsson :	Added protection against "extended FTP ALG attack"
 *	Neil Toronto		: portfw FTP support
 *	Juan Jose Ciarlante	: reimplemented parsing logic, merged portfw FTP support for PASV (new "in_ports" module param), use th->doff for data offset
 *	Juan Jose Ciarlante	: safe_mem_eq2() and size adjustments for less CPU
 *	Juan Jose Ciarlante	: fwmark hook-able
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License.
 *	
 * Multiple Port Support
 *	The helper can be made to handle up to MAX_MASQ_APP_PORTS (normally 12)
 *	with the port numbers being defined at module load time.  The module
 *	uses the symbol "ports" to define a list of monitored ports, which can
 *	be specified on the insmod command line as
 *		ports=x1,x2,x3...
 *	where x[n] are integer port numbers.  This option can be put into
 *	/etc/conf.modules (or /etc/modules.conf depending on your config)
 *	where modload will pick it up should you use modload to load your
 *	modules.
 * Additional portfw Port Support
 * 	Module parameter "in_ports" specifies the list of forwarded ports
 * 	at firewall (portfw and friends) that must be hooked to allow 
 * 	PASV connections to inside servers.
 * 	Same as before:
 * 		in_ports=fw1,fw2,...
 *	Eg: 
 *		ipmasqadm portfw -a -P tcp -L a.b.c.d 2021 -R 192.168.1.1 21
 *		ipmasqadm portfw -a -P tcp -L a.b.c.d 8021 -R 192.168.1.1 21
 *		modprobe ip_masq_ftp in_ports=2021,8021
 * 
 * Protection against the "extended FTP ALG vulnerability".
 *	This vulnerability was reported in:
 *
 *	http://www.securityfocus.com/templates/archive.pike?list=82&date=2000-03-08&msg=38C8C8EE.544524B1@enternet.se
 * 
 *	The protection here is very simplistic, but it at least denies access 
 *	to all ports under 1024, and allows the user to specify an additional 
 *	list of high ports on the insmod command line, like this:
 *		noport=x1,x2,x3, ...
 *	Up to MAX_MASQ_APP_PORTS (normally 12) ports may be specified, the 
 *	default blocks access to the X server (port 6000) only.
 * 
 *	Patch by Bjarni R. Einarsson <bre@netverjar.is>.  The original patch is
 *	available at: http://bre.klaki.net/programs/ip_masq_ftp.2000-03-20.diff
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/init.h>
#include <net/protocol.h>
#include <net/tcp.h>

/* #define IP_MASQ_NDEBUG */
#include <net/ip_masq.h>

/*	
 * 	paranoid, CPU care offset handling
 */
/* #define MASQ_FTP_RELAXED 1 */
#ifdef MASQ_FTP_RELAXED
#define _N(x) 	0
#else
#define _N(x)	(x)
#endif

#define IP_MASQ_FTP_RPAREN 0x01 /* stream has ')' char */

/*
 * 	Eat 1 port (last elem) for holding firewall mark instance
 */
#define MAX_MASQ_FTP_PORTS (MAX_MASQ_APP_PORTS-1)
#define MAX_MASQ_FTP_PORTS_MODPARM 11
/* 
 * List of ports (up to MAX_MASQ_APP_PORTS) to be handled by helper
 * First port is set to the default port.
 */
static int ports[MAX_MASQ_FTP_PORTS] = {21}; /* I rely on the trailing items being set to zero */
static struct ip_masq_app *masq_ftp_objs[MAX_MASQ_APP_PORTS];

/*
 * 	in (forwarded) ports
 */
static int in_ports[MAX_MASQ_FTP_PORTS] = {0}; 
static struct ip_masq_app *masq_in_ftp_objs[MAX_MASQ_APP_PORTS];

#define masq_ftp_mark masq_ftp_objs[MAX_MASQ_APP_PORTS-1]
#define masq_in_ftp_mark masq_in_ftp_objs[MAX_MASQ_APP_PORTS-1]

/*
 * List of ports (up to MAX_MASQ_APP_PORTS) we don't allow ftp-data 
 * connections to.  Default is to block connections to port 6000 (X servers).
 * This is in addition to all ports under 1024.
 */
static int noport[MAX_MASQ_FTP_PORTS] = {6000, 0}; /* I rely on the trailing items being set to zero */

/*
 *	Firewall marks for "normal" and "forw" cases
 */
static int mark=0;
static int in_mark=0;
/*
 *	Debug level
 */
#ifdef CONFIG_IP_MASQ_DEBUG
static int debug=0;
MODULE_PARM(debug, "i");
#endif

MODULE_PARM(ports, "1-" __MODULE_STRING(MAX_MASQ_FTP_PORTS_MODPARM) "i");
MODULE_PARM(in_ports, "1-" __MODULE_STRING(MAX_MASQ_FTP_PORTS_MODPARM) "i");
MODULE_PARM(noport, "1-" __MODULE_STRING(MAX_MASQ_FTP_PORTS_MODPARM) "i");
MODULE_PARM(mark, "i");
MODULE_PARM(in_mark, "i");

/*	Dummy variable */
static int masq_ftp_pasv;

/*
 * This function parses the IP address and Port number found in PORT commands
 * and PASV responses.  This used to be done in-line, but with four cases it
 * seemed worth encapsulating.  It returns the IP address, or zero if an
 * error is detected.
 */
static __u32 parse_ip_port( char **datap, __u16 *portp )
{
	char	*data = *datap;
#if CONFIG_IP_MASQ_DEBUG
	char *data0=data;
#endif
	unsigned char p1,p2,p3,p4,p5,p6;

	p1 = simple_strtoul(data, &data, 10);
	if (*data != ',')
		return 0;
	p2 = simple_strtoul(data+1, &data, 10);
	if (*data != ',')
		return 0;
	p3 = simple_strtoul(data+1, &data, 10);
	if (*data != ',')
		return 0;
	p4 = simple_strtoul(data+1, &data, 10);
	if (*data != ',')
		return 0;
	p5 = simple_strtoul(data+1, &data, 10);
	if (*data != ',')
		return 0;
	p6 = simple_strtoul(data+1, &data, 10);

	IP_MASQ_DEBUG(2-debug, "FTP: parse_ip_port() Ok: \"%*s\" size=%d\n",	
			data-data0,
			data0,
			data-data0);
	*datap = data;
	*portp = (p5<<8) | p6;
	return (p1<<24) | (p2<<16) | (p3<<8) | p4;
}


static int
masq_ftp_init_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_INC_USE_COUNT;
        return 0;
}

static int
masq_ftp_done_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_DEC_USE_COUNT;
        return 0;
}


static int masq_ftp_unsafe(__u32 from_ip, __u16 from_port) {
	int i;
	if (from_port < 1024) 
	{
		IP_MASQ_DEBUG(1-debug, "Unsafe PORT %d.%d.%d.%d:%d detected, ignored\n",NIPQUAD(from_ip),from_port);
		return 1;
	}

	for (i = 0; (i < MAX_MASQ_FTP_PORTS) && (noport[i]); i++)
		if (from_port == noport[i])
		{
			IP_MASQ_DEBUG(1-debug, "Unsafe (module parm) PORT %d.%d.%d.%d:%d detected, ignored\n",NIPQUAD(from_ip),from_port);
			return 1;
		}
	return 0;
}
/*
 * carefully compare with any of these 2 strings, eating stream pointer
 * as it proceeds
 * Returns:
 * 	NULL	not matched
 * 	!NULL	last matched char*
 */
static char* safe_mem_eq2(char *data, const char *data_limit, int size, const char *str1, const char *str2) {
#if CONFIG_IP_MASQ_DEBUG
	const char *data0=data;
	if (!data) {
		IP_MASQ_ERR("FTP: NULL data passed to safe_mem_eq2()!!!");
		return NULL;
	}
#endif
	IP_MASQ_DEBUG(3-debug, "FTP: safe_mem_equal(): datalimit-data=%d, size=%d\n", data_limit-data, size);

	/*	No point in going after data_limit for "size" comparison */
	data_limit -= size;

	while (data <= data_limit) {
		if (memcmp(data,str1,size)==0) 
			goto equal_ok;
		if (str2 && memcmp(data,str2,size)==0) 
			goto equal_ok;
		data++;
	}
	IP_MASQ_DEBUG(2-debug, "FTP: safe_mem_equal(): \"%s\" not matched)\n", str1);
	return NULL;
equal_ok:
	IP_MASQ_DEBUG(2-debug, "FTP: safe_mem_equal(): \"%s\" matched at offset %d\n",
				str1, data-data0);
	return data+size;
}
int
masq_ftp_out (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, __u32 maddr)
{
        struct sk_buff *skb;
	struct iphdr *iph;
	struct tcphdr *th;
	char *p, *data, *data0, *data_limit;
	__u32 from=0;
	__u32 from_n;
	__u16 port;
	struct ip_masq *n_ms;
	char buf[25];		/* xxx.xxx.xxx.xxx,ppp,ppp)\000 */
        unsigned flags=0;    	/* processing flags */
        unsigned buf_len;
	int diff=0;

	/*	Only useful for established  sessions */
	if (ms->state != IP_MASQ_S_ESTABLISHED) 
		return 0;

        skb = *skb_p;
	iph = skb->nh.iph;
        th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
        data = (char *)th + (((struct tcphdr*)th)->doff << 2);
	data0 = data;
        data_limit = skb->h.raw + skb->len;

	IP_MASQ_DEBUG(2-debug, "FTP: called masq_ftp_out() for type=0x%x datasize=%d\n",
			mapp->type,
			data_limit-data);

	/*	Only useful for actual data 	*/
	if (data_limit<=data)
		return 0;

	/*
	 * 	We are about to hack an OUT (from firewall) packet,
	 *	check if 
	 *		OUTBOUND: 	internal client stream
	 *		INBOUND: 	internal server stream
	 */
	
	if (IP_MASQ_APP_TYPE2FLAGS(mapp->type)&IP_MASQ_APP_OUTBOUND) {
		IP_MASQ_DEBUG(1-debug, "FTP: in->out client stream\n");

		/* 
		 * Minimum sizes ...
		 *              PORT x,x,x,x,y,y+...  + \r\n
		 *                   <---------- 11   + 2 = 13
		 */
		if (!(data=safe_mem_eq2(data, data_limit-_N(13),
				5, "PORT ", "port ")) ) {
			if (safe_mem_eq2(data0, data_limit, 6, "PASV\r\n", "pasv\r\n")) {
				/*	Flags this tunnel as pasv, return */
				ms->app_data = &masq_ftp_pasv;
			}
			return 0;
		}
		p = data;
		from = parse_ip_port(&data, &port);
		if (masq_ftp_unsafe(from,port)) 
			return 0;
		IP_MASQ_DEBUG(1-debug,"FTP: out: PORT %d.%d.%d.%d:%d detected\n",
				NIPQUAD(from), port);

	} else if (IP_MASQ_APP_TYPE2FLAGS(mapp->type)&IP_MASQ_APP_INBOUND) {
		IP_MASQ_DEBUG(1-debug, "FTP: in->out server stream\n");

		/* 
		 * Minimum sizes...
		 *  	Entering Passive Mode (x,x,x,x,y,y)+...  + \r\n
		 *  	         <------------------------- 26   + 2 = 28
		 *  	                 <----------------- 18   + 2 = 20
		 *  	                      <------------ 13   + 2 = 15
		 */
		if (!(data=safe_mem_eq2(data, data_limit-_N(28), 8, "ntering ", "NTERING ")))
			return 0;
		if (!(data=safe_mem_eq2(data+_N(1), data_limit-_N(20), 7, "assive ", "ASSIVE ")))
			return 0;
		if (!(data=safe_mem_eq2(data+_N(1), data_limit-_N(15), 4, "ode ", "ODE ")))
			return 0;
		do {
			if (data >= data_limit)
				return 0;
		} while (*data++ != '(');

		p = data;
		from = parse_ip_port(&data, &port);
		if ((from == 0) || (*data++ !=')'))
			return 0;

 		flags |= IP_MASQ_FTP_RPAREN;

	}  else 
		return 0; 

	/* no from detected, give up */
	if (from == 0)
		return 0;

	/* store from in network byte order */
	from_n = htonl(from);
	/*
	 * Now update or create an masquerade entry for it
	 */

	IP_MASQ_DEBUG(1-debug, "FTP: out: %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d\n", 
			NIPQUAD(from_n), htons(port), 
			NIPQUAD(iph->daddr), 0);

	n_ms = ip_masq_out_get(iph->protocol,
				 from_n, htons(port),
				 iph->daddr, 0);
	if (!n_ms) {
		n_ms = ip_masq_new(IPPROTO_TCP,
				   maddr, 0,
				   from_n, htons(port),
				   iph->daddr, 0,
				   IP_MASQ_F_NO_DPORT);

		if (n_ms==NULL)
			return 0;
		ip_masq_control_add(n_ms, ms);
	}

	/*
	 * Replace the old PORT with the new one
	 */
	from = ntohl(n_ms->maddr);
	port = ntohs(n_ms->mport);
	sprintf(buf,"%d,%d,%d,%d,%d,%d%c",
		from>>24&255,from>>16&255,from>>8&255,from&255,
		port>>8&255,port&255,
		(flags&IP_MASQ_FTP_RPAREN)? ')':0);
	buf_len = strlen(buf);

	IP_MASQ_DEBUG(1-debug, "FTP: new PORT %d.%d.%d.%d:%d\n",NIPQUAD(maddr),port);

	/*
	 * Calculate required delta-offset to keep TCP happy
	 */

	diff = buf_len - (data-p);

	/*
	 *	No shift.
	 */

	if (diff==0) {
		/*
		 * simple case, just replace the old PORT cmd
		 */
		memcpy(p,buf,buf_len);
	} else {

		*skb_p = ip_masq_skb_replace(skb, GFP_ATOMIC, p, data-p, buf, buf_len);
	}
	/*
	 * 	Move tunnel to listen state
	 */
	ip_masq_listen(n_ms);
	ip_masq_put(n_ms);

	return diff;

}

/*
 * Look at incoming ftp packets to catch the response to a PASV command.  When
 * we see one we build a masquerading entry for the client address, client port
 * 0 (unknown at the moment), the server address and the server port.  Mark the
 * current masquerade entry as a control channel and point the new entry at the
 * control entry.  All this work just for ftp keepalive across masquerading.
 *
 * The incoming packet should be something like
 * "227 Entering Passive Mode (xxx,xxx,xxx,xxx,ppp,ppp)".
 * xxx,xxx,xxx,xxx is the server address, ppp,ppp is the server port number.
 * ncftp 2.3.0 cheats by skipping the leading number then going 22 bytes into
 * the data so we do the same.  If it's good enough for ncftp then it's good
 * enough for me.
 *
 * In this case, the client is the source machine being masqueraded, the server
 * is the destination for ftp requests.  It all depends on your point of view ...
 */

int
masq_ftp_in (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, __u32 maddr)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct tcphdr *th;
	char *data, *data_limit;
	__u32 to;
	__u32 from_n;
	__u16 port;
	struct ip_masq *n_ms;

	/*	Only useful for established  sessions */
	if (ms->state != IP_MASQ_S_ESTABLISHED) 
		return 0;

	skb = *skb_p;
	iph = skb->nh.iph;
	th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
        data = (char *)th + (((struct tcphdr*)th)->doff << 2);
	data_limit = skb->h.raw + skb->len;

	IP_MASQ_DEBUG(2-debug, "FTP: called masq_ftp_in() for type=0x%x datasize=%d\n", 
			mapp->type,
			data_limit-data);

	/*	Only useful for actual data 	*/
	if (data_limit<=data)
		return 0;

	/*
	 * 	We are about to hack an IN (to firewall) packet,
	 *	check if 
	 *		OUTBOUND: 	internal client stream
	 *		INBOUND: 	internal server stream
	 */

	if (IP_MASQ_APP_TYPE2FLAGS(mapp->type)&IP_MASQ_APP_OUTBOUND) {
		IP_MASQ_DEBUG(1-debug, "FTP: out->in client stream\n");
		/* 	
		 * 	For OUTBOUND only parse on input for linking PASV
		 * 	data tunnel with control.
		 * 	Exit quickly if no outstanding PASV 
		 */
		if (ms->app_data != &masq_ftp_pasv)
			return 0;

		/* 
		 * Minimum sizes...
		 *  	Entering Passive Mode (x,x,x,x,y,y)+...  + \r\n
		 *  	         <------------------------- 26   + 2 = 28
		 *  	                 <----------------- 18   + 2 = 20
		 *  	                      <------------ 13   + 2 = 15
		 */
		if (!(data=safe_mem_eq2(data, data_limit-_N(28), 8, "ntering ", "NTERING ")))
			return 0;
		if (!(data=safe_mem_eq2(data+_N(1), data_limit-_N(20), 6, "ssive ", "SSIVE ")))
			return 0;
		if (!(data=safe_mem_eq2(data+_N(1), data_limit-_N(15), 4, "ode ", "ODE ")))
			return 0;
		do {
			if (data >= data_limit)
				return 0;
		} while (*data++ != '(');

		to = parse_ip_port(&data, &port);
		if (to == 0 || *data != ')')
			return 0;

		from_n = ntohl(ms->saddr);
		IP_MASQ_DEBUG(1-debug, "FTP: PASV response %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d detected\n", 
			NIPQUAD(from_n), 0, 
			NIPQUAD(to), port);

	} else if (IP_MASQ_APP_TYPE2FLAGS(mapp->type)&IP_MASQ_APP_INBOUND) {
		IP_MASQ_DEBUG(1-debug, "FTP: out->in server stream\n");

		if (!(data=safe_mem_eq2(data, data_limit-_N(13), 
				5, "PORT ", "port ")) )
			return 0;
		to = parse_ip_port(&data, &port);
		if (to == 0 || (*data != '\r' && *data != '\n'))
			return 0;

		from_n = ntohl(ms->saddr);
		IP_MASQ_DEBUG(1-debug, "FTP: PORT  %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d detected\n", 
			NIPQUAD(from_n), 0, 
			NIPQUAD(to), port);
	} else
		return 0;
	/*
	 * Now update or create an masquerade entry for it
	 */

	n_ms = ip_masq_out_get(iph->protocol,
				 ms->saddr, 0,
				 htonl(to), htons(port));
	if (!n_ms) {
		n_ms = ip_masq_new(IPPROTO_TCP,
					maddr, 0,
					ms->saddr, 0,
					htonl(to), htons(port),
					IP_MASQ_F_NO_SPORT);

		if (n_ms==NULL)
			return 0;
		ip_masq_control_add(n_ms, ms);
	}

#if 0	/* v0.12 state processing */

	/*
	 * keep for a bit longer than tcp_fin, client may not issue open
	 * to server port before tcp_fin_timeout.
	 */
	n_ms->timeout = ip_masq_expire->tcp_fin_timeout*3;
#endif
	ms->app_data = NULL;
	ip_masq_put(n_ms);

	return 0;	/* no diff required for incoming packets, thank goodness */
}

struct ip_masq_app ip_masq_ftp = {
        NULL,			/* next */
	"ftp",			/* name */
        0,                      /* type */
        0,                      /* n_attach */
        masq_ftp_init_1,        /* ip_masq_init_1 */
        masq_ftp_done_1,        /* ip_masq_done_1 */
        masq_ftp_out,           /* pkt_out */
        masq_ftp_in,            /* pkt_in */
};
static struct ip_masq_app *make_instance(const struct ip_masq_app *mapp_class, int *err)
{
	struct ip_masq_app *mapp = NULL;
	if ((mapp = kmalloc(sizeof(struct ip_masq_app), GFP_KERNEL)))
		memcpy(mapp , mapp_class, sizeof(struct ip_masq_app));
	else
		*err= -ENOMEM;
	return mapp;
}

/*	
 *	Register all solicited ports (last array element is
 * 	reserved for firewall "mark" object
 */
static int register_ports(struct ip_masq_app *mapp_instances[], int *ports, unsigned flags) {
	int i;
	int err=0;
	struct ip_masq_app  *mapp;
	for (i=0; (i<MAX_MASQ_FTP_PORTS_MODPARM); i++) {
		if (ports[i]) {
			if (!(mapp = make_instance(&ip_masq_ftp, &err)))
				goto end;
			if ((err=ip_masq_app_init_proto_port(mapp, flags, IPPROTO_TCP, ports[i]))) 
				goto end_kfree;

			if ((err=register_ip_masq_app_type(mapp)))
				goto end_kfree;
			mapp_instances[i]=mapp;
			IP_MASQ_DEBUG(1-debug, "FTP: loaded support on %sport[%d] = %d, type=0x%x\n", 
					(flags&IP_MASQ_APP_INBOUND)? "in_":"",
					i, ports[i], mapp->type);
		} else {
			/* To be safe, force the incarnation table entry to NULL */
			mapp_instances[i] = NULL;
		}
	}
	return 0;
end_kfree:
	kfree_s(mapp, sizeof(struct ip_masq_app));
end:
	IP_MASQ_ERR("FTP: registration error, quitting\n");
	return err;
}
/*
 * 	Unregister ALL ports, _including_ the (possible) firewall
 * 	mark.
 */
static int unregister_ports(struct ip_masq_app *mapp_instances[]) {
	int i, j, k=0;
	for (i=0; (i<MAX_MASQ_APP_PORTS); i++) {
		if (mapp_instances[i]) {
			if ((j = unregister_ip_masq_app(mapp_instances[i]))) {
				k = j;
			} else {
				IP_MASQ_DEBUG(1-debug, "FTP: unloaded support on port[%d] = %d, type=0x%x\n",
				       i, ports[i], mapp_instances[i]->type);
				kfree(mapp_instances[i]);
				mapp_instances[i] = NULL;
			}
		}
	}
	return k;
}

/*
 * 	ip_masq_ftp initialization
 */

__initfunc(int ip_masq_ftp_init(void))
{
	int ret=0;
	struct ip_masq_app *mapp;
	if (
	(ret=register_ports(masq_ftp_objs, ports, IP_MASQ_APP_OUTBOUND)) ||
	(ret=register_ports(masq_in_ftp_objs, in_ports, IP_MASQ_APP_INBOUND))
	)
		goto end;

	if (mark) {
		if (!(mapp=make_instance(&ip_masq_ftp, &ret)))
			goto end;
		if ((ret=ip_masq_app_init_fwmark(mapp, IP_MASQ_APP_OUTBOUND, mark))) {
			kfree_s(mapp, sizeof (struct ip_masq_app));
			goto end;
		}
		if ((ret=register_ip_masq_app_type(mapp)))
			goto end;
		masq_ftp_mark=mapp;
	}
	if (in_mark) {
		if (!(mapp=make_instance(&ip_masq_ftp, &ret)))
			goto end;
		if ((ret=ip_masq_app_init_fwmark(mapp, IP_MASQ_APP_INBOUND, in_mark))) {
			kfree_s(mapp, sizeof (struct ip_masq_app));
			goto end;
		}
		if ((ret=register_ip_masq_app_type(mapp)))
			goto end;
		masq_in_ftp_mark=mapp;
	}
end:
	return ret;
}

/*
 * 	ip_masq_ftp fin.
 */

int ip_masq_ftp_done(void)
{
	int ret;
	ret = unregister_ports(masq_ftp_objs);
	ret += unregister_ports(masq_in_ftp_objs);
	return ret;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
        if (ip_masq_ftp_init() != 0)
                return -EIO;
        return 0;
}

void cleanup_module(void)
{
        if (ip_masq_ftp_done() != 0)
                printk(KERN_INFO "ip_masq_ftp: can't remove module");
}

#endif /* MODULE */
