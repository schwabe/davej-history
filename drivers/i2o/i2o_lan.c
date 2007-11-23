/*
 * 	linux/drivers/i2o/i2o_lan.c
 *
 *    	I2O LAN CLASS OSM 	Prototyping, June 4th 1999
 *
 *	(C) Copyright 1999 	University of Helsinki,
 *				Department of Computer Science
 *
 *   	This code is still under development / test.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.    
 *
 * 	Authors: 	Auvo H�kkinen <Auvo.Hakkinen@cs.Helsinki.FI>
 *			Juha Siev�nen <Juha.Sievanen@cs.Helsinki.FI>
 *
 *	Tested:		in FDDI environment (using SysKonnect's DDM)
 *			in Ethernet environment (using Intel 82558 DDM proto)
 *
 *	TODO:		batch mode networking
 *			- this one assumes that we always get one packet 
 *			  in a bucket
 *			- we've not been able to test batch replies and 
 *			  batch receives
 *			- error checking / timeouts
 *			- code / test for other LAN classes
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/fddidevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/malloc.h>
#include <linux/trdevice.h>
#include <linux/init.h>
#include <asm/io.h>

#include <linux/errno.h>

#include <linux/i2o.h>
#include "i2o_lan.h"

//#define DRIVERDEBUG
#ifdef DRIVERDEBUG
#define dprintk(s, args...) printk(s, ## args) 
#else
#define dprintk(s, args...)
#endif

#define MAX_LAN_CARDS 4
static struct device *i2o_landevs[MAX_LAN_CARDS+1];
static int unit = -1; 			/* device unit number */

struct i2o_lan_local {
	u8 unit;
	struct i2o_device *i2o_dev;
	int reply_flag; 		/* needed by scalar/table queries */
	u32 packet_tresh;		/* treshold for incoming skb's */	
	struct fddi_statistics stats;   /* see also struct net_device_stats */ 
	unsigned short (*type_trans)(struct sk_buff *, struct device *);
};

/* function prototypes */
static int i2o_lan_receive_post(struct device *dev);
static int i2o_lan_receive_post_reply(struct device *dev, struct i2o_message *m);

/*
 * Module params
 */
static u32 bucketpost = 64;
static u32 bucketthresh = 8;

static void i2o_lan_reply(struct i2o_handler *h, struct i2o_controller *iop, 
			  struct i2o_message *m)
{  
	u32 *msg = (u32 *)m;
	u8 unit  = (u8)(msg[2]>>16); // InitiatorContext
	struct device *dev = i2o_landevs[unit];

    	if (msg[0] & (1<<13)) // Fail bit is set
 	{
 		printk(KERN_ERR "IOP failed to process the msg:\n");
		printk(KERN_ERR "  Cmd = 0x%02X, InitiatorTid = %d, TargetTid = %d\n",
			(msg[1] >> 24) & 0xFF, (msg[1] >> 12) & 0xFFF, msg[1] & 0xFFF);
		printk(KERN_ERR "  FailureCode = 0x%02X\n  Severity = 0x%02X\n  "
			"LowestVersion = 0x%02X\n  HighestVersion = 0x%02X\n",
			 msg[4] >> 24, (msg[4] >> 16) & 0xFF, 
			(msg[4] >> 8) & 0xFF, msg[4] & 0xFF);
		printk(KERN_ERR "  FailingHostUnit = 0x%04X\n  FailingIOP = 0x%03X\n",
			msg[5] >> 16, msg[5] & 0xFFF);
		return;
	}	

#ifdef DRIVERDEBUG
//	if (msg[4] >> 24) 	/* ReqStatus != SUCCESS */
		i2o_report_status(KERN_INFO, "i2o_lan", msg);
#endif

	switch (msg[1] >> 24) {
	case LAN_RECEIVE_POST: 
		if (dev->start) 
			i2o_lan_receive_post_reply(dev,m);
		else { 
			// we are getting unused buckets back
			u8 trl_count  = msg[3] & 0x000000FF; 	
			struct i2o_bucket_descriptor *bucket =
				(struct i2o_bucket_descriptor *)&msg[6];
			do {	
				dprintk("%s: Releasing unused bucket\n",dev->name);
				dev_kfree_skb((struct sk_buff *)bucket->context);
				bucket++;
			} while (--trl_count);
		}			
	break;

	case LAN_PACKET_SEND:
	case LAN_SDU_SEND: 
	{
		u8 trl_count  = msg[3] & 0x000000FF; 	
		
		if (msg[4] >> 24) 	// ReqStatus != SUCCESS
			i2o_report_status(KERN_WARNING, dev->name, msg);

		do { 	// The HDM has handled the outgoing packet
			dev_kfree_skb((struct sk_buff *)msg[4 + trl_count]);
			dprintk(KERN_INFO "%s: Request skb freed (trl_count=%d).\n",
				dev->name,trl_count);
		} while (--trl_count);
		
		dev->tbusy = 0;
		mark_bh(NET_BH); /* inform upper layers */
	}
	break;	

	default: 
		if (msg[2] & 0x80000000)  // reply to a UtilParamsGet/Set
		{	
	       		int *flag = (int *)msg[3]; // flag for i2o_post_wait
			if (msg[4] >> 24) // ReqStatus != SUCCESS
			{
				i2o_report_status(KERN_WARNING, dev->name, msg);
	      		 	*flag = -(msg[4] & 0xFFFF); // DetailedStatus
	      		}
	      		else
	      			*flag = I2O_POST_WAIT_OK;
		}
	}
}

static struct i2o_handler i2o_lan_handler =
{
	i2o_lan_reply,
	"I2O Lan OSM",
	0		// context
};
static int lan_context;


static int i2o_lan_receive_post_reply(struct device *dev, struct i2o_message *m)
{
	u32 *msg = (u32 *)m;
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
	struct i2o_bucket_descriptor *bucket = (struct i2o_bucket_descriptor *)&msg[6];
	struct i2o_packet_info *packet;
	
	u8 trl_count  = msg[3] & 0x000000FF; 	
	struct sk_buff *skb, *newskb;

#if 0
	dprintk(KERN_INFO "TrlFlags = 0x%02X, TrlElementSize = %d, TrlCount = %d\n"
		"msgsize = %d, buckets_remaining = %d\n", 
		msg[3]>>24, msg[3]&0x0000FF00, trl_count, msg[0]>>16, msg[5]);	
#endif
	dprintk(KERN_INFO "Buckets_remaining = %d\n",msg[5]);

	do {
		skb = (struct sk_buff *)(bucket->context);		
		packet = (struct i2o_packet_info *)bucket->packet_info;	
#if 0
		dprintk(KERN_INFO "flags = 0x%02X, offset = 0x%06X, status = 0x%02X, length = %d\n",
			packet->flags, packet->offset, packet->status, packet->len);
#endif
		if (packet->len < priv->packet_tresh) {
			newskb = (struct sk_buff *)
					dev_alloc_skb(packet->len+2);	
			if (newskb) {
				skb_reserve(newskb,2);
				memcpy(skb_put(newskb,packet->len), 
				       skb->data, packet->len);
				newskb->dev = dev;
				newskb->protocol = priv->type_trans(newskb, dev);

				netif_rx(newskb);
				dev_kfree_skb(skb); // FIXME: reuse this skb? 
			}
			else {
				printk("Can't allocate skb.\n");
				return -ENOMEM;
			}
		} else {
			skb_put(skb,packet->len);		
			skb->dev  = dev;		
			skb->protocol = priv->type_trans(skb, dev);

			netif_rx(skb);
		}
		dprintk(KERN_INFO "%s: Incoming packet (%d bytes) delivered "
			"to upper level.\n",dev->name,packet->len);

		bucket++; // to next Packet Descriptor Block
	} while (--trl_count);


	if (msg[5] <= bucketthresh)  	// BucketsRemaining
		i2o_lan_receive_post(dev);

	return 0;
}

/*  ====================================================
 *  Interface to i2o:  functions to send lan class request 
 */


/* 
 * i2o_lan_receive_post(): Post buckets to receive packets.
 */
static int i2o_lan_receive_post(struct device *dev)
{	
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;
	struct sk_buff *skb;
	u32 m; u32 *msg;
	
	u32 bucket_len = (dev->mtu + dev->hard_header_len);
	u32 bucket_count;
	int n_elems = (iop->inbound_size - 16 ) / 12; /* msg header + SGLs */
	u32 total = 0;
	int i;

	while (total < bucketpost)
	{
		m = I2O_POST_READ32(iop);
		if (m == 0xFFFFFFFF)
			return -ETIMEDOUT;		
		msg = bus_to_virt(iop->mem_offset + m);
	
		bucket_count = (total + n_elems < bucketpost)
			     ? n_elems
			     : bucketpost - total;

		msg[0] = I2O_MESSAGE_SIZE(4 + 3 *  bucket_count) | SGL_OFFSET_4;
		msg[1] = LAN_RECEIVE_POST<<24 | HOST_TID<<12 | i2o_dev->id;
		msg[2] = priv->unit << 16 | lan_context; // InitiatorContext	
		msg[3] = bucket_count;			 // BucketCount

		for (i = 0; i < bucket_count; i++)
		{
			skb = dev_alloc_skb(bucket_len + 2);
			if (skb == NULL)
				return -ENOMEM;
			skb_reserve(skb, 2);
			msg[4 + 3*i] = 0x51000000 | bucket_len;
			msg[5 + 3*i] = (u32)skb;
			msg[6 + 3*i] = virt_to_bus(skb->data);
		}
		msg[4 + 3*i - 3] |= 0x80000000; // set LE flag
		i2o_post_message(iop,m);

		dprintk(KERN_INFO "%s: Sending %d buckets (size %d) to LAN HDM.\n",
			dev->name, bucket_count, bucket_len);

		total += bucket_count;
	}
	return 0;	
}	

/* 
 * i2o_lan_reset(): Reset the LAN adapter into the operational state and 
 * 	restore it to full operation.
 */
static int i2o_lan_reset(struct device *dev) 
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;	
	u32 msg[5];

	msg[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
	msg[1] = LAN_RESET<<24 | HOST_TID<<12 | i2o_dev->id;
	msg[2] = priv->unit << 16 | lan_context; // InitiatorContext
	msg[3] = 0; 				 // TransactionContext
	msg[4] = 1 << 16; 			 // return posted buckets

	if (i2o_post_this(iop, i2o_dev->id, msg, sizeof(msg)) < 0)
		return -ETIMEDOUT;		

	return 0; 
}

/* 
 * i2o_lan_suspend(): Put LAN adapter into a safe, non-active state.
 * 	Reply to any LAN class message with status error_no_data_transfer 
 *	/ suspended.
 */
static int i2o_lan_suspend(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;	
	u32 msg[5];

	msg[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
	msg[1] = LAN_SUSPEND<<24 | HOST_TID<<12 | i2o_dev->id;
	msg[2] = priv->unit << 16 | lan_context; // InitiatorContext
	msg[3] = 0; 				 // TransactionContext
	msg[4] = 1 << 16; 			 // return posted buckets

	if (i2o_post_this(iop, i2o_dev->id, msg, sizeof(msg)) < 0)
		return -ETIMEDOUT;

	return 0;
}

/*
 * Set DDM into batch mode.
 */
static void i2o_set_batch_mode(struct device *dev)
{

/* 
 * NOTE: we have not been able to test batch mode
 * 	 since HDMs we have, don't implement it 
 */

	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
	struct i2o_controller *iop = i2o_dev->controller;	
	u32 val;

	/* set LAN_BATCH_CONTROL attributes */

	// enable batch mode, toggle automatically
	val = 0x00000000;
//	val = 0x00000001; // turn off batch mode
	if (i2o_set_scalar(iop, i2o_dev->id, lan_context, 0x0003, 0,
			&val, 4, &priv->reply_flag) <0)
		printk(KERN_WARNING "Unable to enter I2O LAN batch mode.\n");
	else
		dprintk(KERN_INFO "%s: I2O LAN batch mode enabled.\n",dev->name);
//		dprintk(KERN_INFO "%s: I2O LAN batch mode disabled.\n",dev->name);
	/*
	 * When PacketOrphanlimit is same as the maximum packet length,
	 * the packets will never be split into two separate buckets
	 */

	/* set LAN_OPERATION attributes */

	val = dev->mtu + dev->hard_header_len; // PacketOrphanLimit
	if (i2o_set_scalar(iop, i2o_dev->id, lan_context, 0x0004, 2,
		&val, 4, &priv->reply_flag) < 0)
		printk(KERN_WARNING "i2o_lan: Unable to set PacketOrphanLimit.\n");
	else
		dprintk(KERN_INFO "%s: PacketOrphanLimit set to %d\n",
			dev->name,val);

	return;	
}

/* 
 * i2o_lan_open(): Open the device to send/receive packets via 
 * the network device.	
 */
static int i2o_lan_open(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
	struct i2o_controller *iop = i2o_dev->controller;

	if (i2o_issue_claim(iop, i2o_dev->id, lan_context, 1, 
			    &priv->reply_flag) < 0)
	{
		printk(KERN_WARNING "%s: Unable to claim the I2O LAN device.\n", dev->name);
		return -EAGAIN;
	}
	dprintk(KERN_INFO "%s: I2O LAN device claimed (tid=%d).\n", dev->name, i2o_dev->id);

	i2o_lan_reset(dev);
	
	dev->tbusy = 0;
	dev->start = 1;

	priv->packet_tresh = dev->mtu - (dev->mtu >> 3);

	i2o_set_batch_mode(dev);
	i2o_lan_receive_post(dev);

	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * i2o_lan_close(): End the transfering.
 */ 
static int i2o_lan_close(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
	struct i2o_controller *iop = i2o_dev->controller;	

	dev->tbusy = 1;
	dev->start = 0;

// This is the right place for LanSuspend, but it seems to cause 
// a kernel crash when we are using 82558 HDM proto

	i2o_lan_suspend(dev);

	if (i2o_issue_claim(iop, i2o_dev->id, lan_context, 0, 
			    &priv->reply_flag) < 0)
		printk(KERN_WARNING "%s: Unable to unclaim I2O LAN device "
		       "(tid=%d)\n", dev->name, i2o_dev->id);

	MOD_DEC_USE_COUNT;

	return 0;
}

/* 
 * i2o_lan_sdu_send(): Send a packet, MAC header added by the HDM.
 * Must be supported by Fibre Channel, optional for Ethernet/802.3, 
 * Token Ring, FDDI
 */
static int i2o_lan_sdu_send(struct sk_buff *skb, struct device *dev)
{	
#if 0
/* not yet tested */
        struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv; 
        struct i2o_device *i2o_dev = priv->i2o_dev;     
        struct i2o_controller *iop = i2o_dev->controller;
        u32 m; u32 *msg;

	dprintk(KERN_INFO "LanSDUSend called, skb->len = %d\n", skb->len);

        m = I2O_POST_READ32(iop);
	if (m == 0xFFFFFFFF) 
	{
		dev_kfree_skb(skb);
		return -ETIMEDOUT;
	}    
	msg = bus_to_virt(iop->mem_offset + m);

        msg[0] = NINE_WORD_MSG_SIZE | SGL_OFFSET_4;
        msg[1] = LAN_SDU_SEND<<24 | HOST_TID<<12 | i2o_dev->id; 
        msg[2] = priv->unit << 16 | lan_context; // IntiatorContext
        msg[3] = 1<<4;		// TransmitControlWord: suppress CRC generation

        // create a simple SGL, see fig. 3-26
        // D7 = 1101 0111 = LE eob 0 1 LA dir bc1 bc0

        msg[4] = 0xD7000000 | (skb->len);  // no MAC hdr included     
        msg[5] = (u32)skb;		   // TransactionContext
        memcpy(&msg[6], skb->data, 8);	   // Destination MAC Addr ??
        msg[7] &= 0x0000FFFF;		   // followed by two bytes zeros
        msg[8] = virt_to_bus(skb->data);
        dev->trans_start = jiffies;
	i2o_post_message(iop,m);

	dprintk(KERN_INFO "%s: Packet (%d bytes) sent to network.\n",
		dev->name,skb->len);
#endif
        return 0;
}

/* 
 * i2o_lan_packet_send(): Send a packet as is, including the MAC header.
 * 
 * Must be supported by Ethernet/802.3, Token Ring, FDDI, optional for 
 * Fibre Channel
 */ 
static int i2o_lan_packet_send(struct sk_buff *skb, struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
	struct i2o_controller *iop = i2o_dev->controller;
        u32 m; u32 *msg;

        m = I2O_POST_READ32(iop);
	if (m == 0xFFFFFFFF) {
		dev_kfree_skb(skb);
		return -ETIMEDOUT;
	}    
	msg = bus_to_virt(iop->mem_offset + m);
	
	msg[0] = SEVEN_WORD_MSG_SIZE | 1<<12 | SGL_OFFSET_4;
	msg[1] = LAN_PACKET_SEND<<24 | HOST_TID<<12 | i2o_dev->id;	
	msg[2] = priv->unit << 16 | lan_context; // IntiatorContext
	msg[3] = 1 << 4; 			 // TransmitControlWord
	
	// create a simple SGL, see fig. 3-26
	// D5 = 1101 0101 = LE eob 0 1 LA dir bc1 bc0

	msg[4] = 0xD5000000 | skb->len;		// MAC hdr included
	msg[5] = (u32)skb; 			// TransactionContext
	msg[6] = virt_to_bus(skb->data);

        i2o_post_message(iop,m);
        
	dprintk(KERN_INFO "%s: Packet (%d bytes) sent to network.\n",
		dev->name, skb->len);

	return 0;
}

static struct net_device_stats *i2o_lan_get_stats(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;
	u64 val64[16];
	u64 supported_group[4] = { 0, 0, 0, 0 };

        if (i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0100, -1, 
        		 val64, sizeof(val64), &priv->reply_flag) < 0)
        	dprintk("%s: Unable to query LAN_HISTORICAL_STATS.\n",dev->name);
	else {
        	dprintk("%s: LAN_HISTORICAL_STATS queried.\n",dev->name);
        	priv->stats.tx_packets = val64[0];
        	priv->stats.tx_bytes   = val64[1];
        	priv->stats.rx_packets = val64[2];
        	priv->stats.rx_bytes   = val64[3];
        	priv->stats.tx_errors  = val64[4];
        	priv->stats.rx_errors  = val64[5];
		priv->stats.rx_dropped = val64[6];
	}

        i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0180, -1, 
        		&supported_group, sizeof(supported_group), &priv->reply_flag);

	if (supported_group[2]) {
        	if (i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0183, -1, 
        	 	val64, sizeof(val64), &priv->reply_flag) < 0)
        		dprintk("%s: Unable to query LAN_OPTIONAL_RX_HISTORICAL_STATS.\n",dev->name);
		else {
        		dprintk("%s: LAN_OPTIONAL_RX_HISTORICAL_STATS queried.\n",dev->name);
			priv->stats.multicast        = val64[4];
			priv->stats.rx_length_errors = val64[10];
			priv->stats.rx_crc_errors    = val64[0];
		}
	}

	if (i2o_dev->subclass == I2O_LAN_ETHERNET)
	{
		u64 supported_stats = 0;		

        	if (i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0200, -1, 
        			 val64, sizeof(val64), &priv->reply_flag) < 0)
        		dprintk("%s: Unable to query LAN_802_3_HISTORICAL_STATS.\n",dev->name);
		else {
        		dprintk("%s: LAN_802_3_HISTORICAL_STATS queried.\n",dev->name);
	 		priv->stats.transmit_collision = val64[1] + val64[2];
			priv->stats.rx_frame_errors    = val64[0];		
			priv->stats.tx_carrier_errors  = val64[6];
		}
	
        	i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0280, -1, 
        			 &supported_stats, 8, &priv->reply_flag);
        			
        	if (supported_stats != 0) {
        		if (i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0281, -1, 
        				 val64, sizeof(val64), &priv->reply_flag) < 0)
        			dprintk("%s: Unable to query LAN_OPTIONLA_802_3_HISTORICAL_STATS.\n",dev->name);
			else {
        			dprintk("%s: LAN_OPTIONAL_802_3_HISTORICAL_STATS queried.\n",dev->name);
				if (supported_stats & 0x1)
					priv->stats.rx_over_errors = val64[0];
				if (supported_stats & 0x4)
					priv->stats.tx_heartbeat_errors = val64[2];
			}
		}
	}

#ifdef CONFIG_TR
	if (i2o_dev->subclass == I2O_LAN_TR)
	{
        	if (i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0300, -1, 
        			 val64, sizeof(val64), &priv->reply_flag) < 0)
        		dprintk("%s: Unable to query LAN_802_5_HISTORICAL_STATS.\n",dev->name);
		else {
			struct tr_statistics *stats = 
					(struct tr_statistics *)&priv->stats;
        		dprintk("%s: LAN_802_5_HISTORICAL_STATS queried.\n",dev->name);

			stats->line_errors		= val64[0];
			stats->internal_errors		= val64[7];
			stats->burst_errors		= val64[4];
			stats->A_C_errors		= val64[2];
			stats->abort_delimiters		= val64[3];
			stats->lost_frames		= val64[1];
			/* stats->recv_congest_count	= ?;  FIXME ??*/
			stats->frame_copied_errors	= val64[5];
			stats->frequency_errors		= val64[6];
			stats->token_errors		= val64[9];
		}
		/* Token Ring optional stats not yet defined */
	}
#endif

#ifdef CONFIG_FDDI
	if (i2o_dev->subclass == I2O_LAN_FDDI)
	{
        	if (i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0400, -1, 
        			 val64, sizeof(val64), &priv->reply_flag) < 0)
        		dprintk("%s: Unable to query LAN_FDDI_HISTORICAL_STATS.\n",dev->name);
		else {
        		dprintk("%s: LAN_FDDI_HISTORICAL_STATS queried.\n",dev->name);
			priv->stats.smt_cf_state = val64[0];
			memcpy(priv->stats.mac_upstream_nbr, &val64[1], FDDI_K_ALEN);
			memcpy(priv->stats.mac_downstream_nbr, &val64[2], FDDI_K_ALEN);			
			priv->stats.mac_error_cts = val64[3];
			priv->stats.mac_lost_cts  = val64[4];
			priv->stats.mac_rmt_state = val64[5];
			memcpy(priv->stats.port_lct_fail_cts, &val64[6], 8);
			memcpy(priv->stats.port_lem_reject_cts, &val64[7], 8);	
			memcpy(priv->stats.port_lem_cts, &val64[8], 8);
			memcpy(priv->stats.port_pcm_state, &val64[9], 8);
		}
		/* FDDI optional stats not yet defined */
	}		
#endif

	return (struct net_device_stats *)&priv->stats;
}
 
/*
 * i2o_lan_set_multicast_list(): Enable a network device to receive packets
 *	not send to the protocol address.
 */
static void i2o_lan_set_multicast_list(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;
	u32 filter_mask;
	u32 work32[64];

	dprintk(KERN_INFO "%s: Entered i2o_lan_set_multicast_list().\n", dev->name);

return;

/* FIXME: Why does the next call kill the interrupt handler?
 * The same works fine in function lan_open(), and in i2o_proc.c
 *
 *  *because its trying to sleep in an irq - this must be async - Alan
 */

	if (i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0001, -1,
			     &work32, sizeof(work32), &priv->reply_flag) < 0 ) 
	{
		printk(KERN_WARNING "i2o_lan: Unable to query "
			" LAN_MAC_ADDRESS table.\n");	
		return;
	}
	printk(KERN_INFO "capab mask = 0x%08X, filter mask = 0x%08X\n",
		work32[7], work32[6]);

	filter_mask = work32[6];

	if (dev->flags & IFF_PROMISC)
	{
		filter_mask |= 0x00000002; 
		dprintk(KERN_INFO "i2o_lan: Enabling promiscuous mode...\n");
        }
        
 	else if ((dev->flags & IFF_ALLMULTI) || dev->mc_count > work32[5])
 	{
		filter_mask |= 0x00000000; 
		dprintk(KERN_INFO "i2o_lan: Enabling all multicast mode...\n");
	}

        else if (dev->mc_count)
	{	
		struct dev_mc_list *mclist;
		int i;
		u8 *work8 = (u8 *)work32;

		dprintk(KERN_INFO "i2o_lan: Enabling multicast mode...\n");
                filter_mask = 0x00000004; 

		/* Fill the multicast addresses */
		mclist = dev->mc_list;

		for (i = 0; i < dev->mc_count; i++)
		{	
			memcpy(work8, mclist->dmi_addr, mclist->dmi_addrlen);
			work8 += 8;
			mclist = mclist->next;
		} 

		if (i2o_clear_table(iop, i2o_dev->id, lan_context, 0x0002,
				&priv->reply_flag) < 0 ) 
			dprintk("%s: Unable to clear LAN_MULTICAST_MAC_ADDRESS table.\n",dev->name);

		if (i2o_row_add_table(iop, i2o_dev->id, lan_context, 0x0002, -1,
			work32, dev->mc_count*8, &priv->reply_flag) < 0)	
			dprintk("%s: Unable to set LAN_MULTICAST_MAC_ADDRESS table.\n",dev->name);
	} 
	
	else {
        	filter_mask |= 0x00000300; // Broadcast, Multicast disabled
		dprintk(KERN_INFO "i2o_lan: Enabling unicast mode...\n");
        }

	if (i2o_set_scalar(iop, i2o_dev->id, lan_context, 0x0001, 3,
			&filter_mask, 4, &priv->reply_flag) <0)
		printk(KERN_WARNING "i2o_lan: Unable to set MAC FilterMask.\n");

	return;
}

struct device *i2o_lan_register_device(struct i2o_device *i2o_dev)
{
	struct device *dev = NULL;
	struct i2o_lan_local *priv = NULL;
	u8 hw_addr[8];
	unsigned short (*type_trans)(struct sk_buff *, struct device *);
	void (*unregister_dev)(struct device *dev);

	switch (i2o_dev->subclass)
	{
	case I2O_LAN_ETHERNET:
        	dev = init_etherdev(NULL, sizeof(struct i2o_lan_local));
		if (dev == NULL)
			return NULL;
		type_trans = eth_type_trans;
		unregister_dev = unregister_netdev;
		break;

#ifdef CONFIG_ANYLAN
	case I2O_LAN_100VG:
		printk(KERN_ERR "i2o_lan: 100base VG not yet supported\n");
		break;
#endif

#ifdef CONFIG_TR
	case I2O_LAN_TR:
		dev = init_trdev(NULL, sizeof(struct i2o_lan_local));
		if (dev==NULL)
			return NULL;
		type_trans = tr_type_trans;
		unregister_dev = unregister_trdev;		
		break;
#endif

#ifdef CONFIG_FDDI
	case I2O_LAN_FDDI:
	{
		int size = sizeof(struct device) + sizeof(struct i2o_lan_local)
			   + sizeof("fddi%d ");

        	dev = (struct device *) kmalloc(size, GFP_KERNEL);
        	memset((char *)dev, 0, size);
            	dev->priv = (void *)(dev + 1);
                dev->name = (char *)(dev + 1) + sizeof(struct i2o_lan_local);

		if (dev_alloc_name(dev,"fddi%d") < 0)
		{
			printk(KERN_WARNING "i2o_lan: Too many FDDI devices.\n");
			kfree(dev);
			return NULL;
		}
		type_trans = fddi_type_trans;
		unregister_dev = (void *)unregister_netdevice;
		
		fddi_setup(dev);
		register_netdev(dev);
      	}
	break;
#endif

#ifdef CONFIG_FIBRE_CHANNEL
	case I2O_LAN_FIBRE_CHANNEL:
		printk(KERN_INFO "i2o_lan: Fibre Channel not yet supported\n");
	break;
#endif

	case I2O_LAN_UNKNOWN:
	default:
		printk(KERN_ERR "i2o_lan: LAN type 0x%08X not supported\n",
		       i2o_dev->subclass);
		return NULL;
	}

	priv = (struct i2o_lan_local *)dev->priv;
	priv->i2o_dev = i2o_dev;
	priv->type_trans = type_trans;
	
        if (i2o_query_scalar(i2o_dev->controller, i2o_dev->id, lan_context,
			     0x0001, 0, &hw_addr, 8, &priv->reply_flag) < 0)
	{
        	printk(KERN_ERR "%s: Unable to query hardware address.\n", dev->name);
		unregister_dev(dev);
		kfree(dev);
      		return NULL;  
        }

	dprintk("%s: hwaddr = %02X:%02X:%02X:%02X:%02X:%02X\n",
      		dev->name,hw_addr[0], hw_addr[1], hw_addr[2], hw_addr[3],
      		hw_addr[4], hw_addr[5]);

	dev->addr_len = 6;
	memcpy(dev->dev_addr, hw_addr, 6);

	dev->open               = i2o_lan_open;
	dev->stop               = i2o_lan_close;
	dev->hard_start_xmit    = i2o_lan_packet_send;
	dev->get_stats          = i2o_lan_get_stats;
	dev->set_multicast_list = i2o_lan_set_multicast_list;

	return dev;
}

#ifdef MODULE
#define i2o_lan_init	init_module
#endif

__init int i2o_lan_init(void)
{
	struct device *dev;
	struct i2o_lan_local *priv;
        int i;

        if (i2o_install_handler(&i2o_lan_handler) < 0)
	{
 		printk(KERN_ERR "Unable to register I2O LAN OSM.\n");
		return -EINVAL;
        }   

	lan_context = i2o_lan_handler.context;

        for (i=0; i < MAX_I2O_CONTROLLERS; i++)
	{
                struct i2o_controller *iop = i2o_find_controller(i);
                struct i2o_device *i2o_dev;

                if (iop==NULL)
			continue;

                for (i2o_dev=iop->devices;i2o_dev != NULL;i2o_dev=i2o_dev->next)
		{
                        if (i2o_dev->class != I2O_CLASS_LAN) 
                                continue;

			if (unit == MAX_LAN_CARDS)
			{
				i2o_unlock_controller(iop);
				printk(KERN_WARNING "Too many I2O LAN devices.\n");
				return -EINVAL;
			}

			dev = i2o_lan_register_device(i2o_dev);
 			if (dev == NULL)
			{
				printk(KERN_ERR "Unable to register I2O LAN device\n");
				continue; // try next one
			}
			priv = (struct i2o_lan_local *)dev->priv;	

			unit++;
			i2o_landevs[unit] = dev;
			priv->unit = unit;

			printk(KERN_INFO "%s: I2O LAN device registered, tid = %d,"
				" subclass = 0x%08X, unit = %d.\n",
				dev->name, i2o_dev->id, i2o_dev->subclass, 
				priv->unit);
                }
		i2o_unlock_controller(iop);
        }

	dprintk(KERN_INFO "%d I2O LAN devices found and registered.\n", unit+1);

	return 0;
}

#ifdef MODULE

void cleanup_module(void)
{
	int i;

	for (i = 0; i <= unit; i++)
	{
		struct device *dev = i2o_landevs[i];
		struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
		struct i2o_device *i2o_dev = priv->i2o_dev;	

		switch (i2o_dev->subclass)
		{
		case I2O_LAN_ETHERNET:
			unregister_netdev(dev);
			kfree(dev);
			break;
#ifdef CONFIG_FDDI
		case I2O_LAN_FDDI:
			unregister_netdevice(dev);
			kfree(dev);
			break;
#endif
#ifdef CONFIG_TR
		case I2O_LAN_TR:
			unregister_trdev(dev);
			kfree(dev);
			break;
#endif
		default:
			printk(KERN_WARNING "i2o_lan: Spurious I2O LAN subclass 0x%08X.\n",
			       i2o_dev->subclass);
		}

		dprintk(KERN_INFO "%s: I2O LAN device unregistered.\n", 
			dev->name);
	}

        i2o_remove_handler(&i2o_lan_handler);
}

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Univ of Helsinki, CS Department");
MODULE_DESCRIPTION("I2O Lan OSM");

MODULE_PARM(bucketpost, "i");           // Number of buckets to post
MODULE_PARM(bucketthresh, "i"); // Bucket post threshold

#endif    