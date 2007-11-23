/*
 * NET3:	Garbage Collector For AF_UNIX sockets
 *
 * Garbage Collector:
 *	Copyright (C) Barak A. Pearlmutter.
 *	Released under the GPL version 2 or later.
 *
 * 12/3/97 -- Flood
 * Internal stack is only allocated one page.  On systems with NR_FILE
 * > 1024, this makes it quite easy for a user-space program to open
 * a large number of AF_UNIX domain sockets, causing the garbage
 * collection routines to run up against the wall (and panic).
 * Changed the MAX_STACK to be associated to the system-wide open file
 * maximum, and use vmalloc() instead of get_free_page() [as more than
 * one page may be necessary].  As noted below, this should ideally be
 * done with a linked list.  
 *
 * Chopped about by Alan Cox 22/3/96 to make it fit the AF_UNIX socket problem.
 * If it doesn't work blame me, it worked when Barak sent it.
 *
 * Assumptions:
 *
 *  - object w/ a bit
 *  - free list
 *
 * Current optimizations:
 *
 *  - explicit stack instead of recursion
 *  - tail recurse on first born instead of immediate push/pop
 *
 *  Future optimizations:
 *
 *  - don't just push entire root set; process in place
 *  - use linked list for internal stack
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *  Fixes:
 *     Al Viro         11 Oct 1998
 *             Graph may have cycles. That is, we can send the descriptor
 *             of foo to bar and vice versa. Current code chokes on that.
 *             Fix: move SCM_RIGHTS ones into the separate list and then
 *             kfree_skb() them all instead of doing explicit fput's.
 *             Another problem: since fput() may block somebody may
 *             create a new unix_socket when we are in the middle of sweep
 *             phase. Fix: revert the logic wrt MARKED. Mark everything
 *             upon the beginning and unmark non-junk ones.
 *
 *             [12 Oct 1998] AAARGH! New code purges all SCM_RIGHTS
 *             sent to connect()'ed but still not accept()'ed sockets.
 *             Fixed. Old code had slightly different problem here:
 *             extra fput() in situation when we passed the descriptor via
 *             such socket and closed it (descriptor). That would happen on
 *             each unix_gc() until the accept(). Since the struct file in
 *             question would go to the free list and might be reused...
 *             That might be the reason of random oopses on close_fp() in
 *             unrelated processes.
 *
 */
 
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/un.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <asm/segment.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/af_unix.h>
#include <linux/proc_fs.h>

/* Internal data structures and random procedures: */

static unix_socket **stack;	/* stack of objects to mark */
static int in_stack = 0;	/* first free entry in stack */
static int max_stack;		/* Calculated in unix_gc() */

extern inline unix_socket *unix_get_socket(struct file *filp)
{
	unix_socket * u_sock = NULL;
	struct inode *inode = filp->f_inode;

	/*
	 *	Socket ?
	 */
	if (inode && inode->i_sock) {
		struct socket * s = &inode->u.socket_i;

		/*
		 *	AF_UNIX ?
		 */
		if (s->ops == &unix_proto_ops)
			u_sock = s->data;
	}
	return u_sock;
}

/*
 *	Keep the number of times in flight count for the file
 *	descriptor if it is for an AF_UNIX socket.
 */
 
void unix_inflight(struct file *fp)
{
	unix_socket *s=unix_get_socket(fp);
	if(s)
		s->protinfo.af_unix.inflight++;
}

void unix_notinflight(struct file *fp)
{
	unix_socket *s=unix_get_socket(fp);
	if(s)
		s->protinfo.af_unix.inflight--;
}


/*
 *	Garbage Collector Support Functions
 */
 
extern inline void push_stack(unix_socket *x)
{
	if (in_stack == max_stack)
		panic("can't push onto full stack");
	stack[in_stack++] = x;
}

extern inline unix_socket *pop_stack(void)
{
	if (in_stack == 0)
		panic("can't pop empty gc stack");
	return stack[--in_stack];
}

extern inline int empty_stack(void)
{
	return in_stack == 0;
}

extern inline void maybe_unmark_and_push(unix_socket *x)
{
	if (!(x->protinfo.af_unix.marksweep&MARKED))
		return;
	x->protinfo.af_unix.marksweep&=~MARKED;
	push_stack(x);
}


/* The external entry point: unix_gc() */

void unix_gc(void)
{
	static int in_unix_gc=0;
	unix_socket *s;
	struct sk_buff *skb;
	struct sk_buff_head hitlist;
		
	/*
	 *	Avoid a recursive GC.
	 */

	if(in_unix_gc)
		return;
	in_unix_gc=1;

	max_stack = max_files;

	stack=(unix_socket **)vmalloc(max_stack * sizeof(unix_socket **));
	if (!stack) {
		in_unix_gc=0;
		return;
	}
	
	/*
	 *	Everything is now marked
	 */

	for(s=unix_socket_list;s!=NULL;s=s->next)
	{
		s->protinfo.af_unix.marksweep|=MARKED;
	}
	
	/* Invariant to be maintained:
		- everything unmarked is either:
		-- (a) on the stack, or
		-- (b) has all of its children unmarked
		- everything on the stack is always unmarked
		- nothing is ever pushed onto the stack twice, because:
		-- nothing previously unmarked is ever pushed on the stack
	 */

	/*
	 *	Push root set
	 */

	for(s=unix_socket_list;s!=NULL;s=s->next)
	{
		/*
		 *	If all instances of the descriptor are not
		 *	in flight we are in use.
		 */
		if(s->socket && s->socket->file && 
			s->socket->file->f_count > s->protinfo.af_unix.inflight)
			maybe_unmark_and_push(s);
	}

	/*
	 *	Mark phase
	 */

	while (!empty_stack())
	{
		unix_socket *x = pop_stack();
		unix_socket *f=NULL,*sk;
tail:		
		skb=skb_peek(&x->receive_queue);
		
		/*
		 *	Loop through all but first born 
		 */
		
		while(skb && skb != (struct sk_buff *)&x->receive_queue)
		{
			/*
			 *	Do we have file descriptors ?
			 */
			if(skb->h.filp)
			{
				/*
				 *	Process the descriptors of this socket
				 */
				int nfd=*(int *)skb->h.filp;
				struct file **fp=(struct file **)(skb->h.filp+sizeof(int));
				while(nfd--)
				{
					/*
					 *	Get the socket the fd matches if
					 *	it indeed does so
					 */
					if((sk=unix_get_socket(*fp++))!=NULL)
					{
						/*
						 *	Remember the first, unmark the
						 *	rest.
						 */
						if(f==NULL)
							f=sk;
						else
							maybe_unmark_and_push(sk);
					}
				}
			}
			/* 
			 *	If we are connecting we need to handle this too
			 */
			if(x->state == TCP_LISTEN)
			{
				if(f==NULL)
					f=skb->sk;
				else
					maybe_unmark_and_push(skb->sk);
			}			 
			skb=skb->next;
		}

		/*
		 *	Handle first born specially 
		 */

		if (f) 
		{
			if (f->protinfo.af_unix.marksweep&MARKED)
			{
				f->protinfo.af_unix.marksweep&=~MARKED;
				x=f;
				f=NULL;
				goto tail;
			}
		}
	}

	skb_queue_head_init(&hitlist);

	for(s=unix_socket_list;s!=NULL;s=s->next)
	{
		if (s->protinfo.af_unix.marksweep&MARKED)
		{
			struct sk_buff *nextsk;
			skb=skb_peek(&s->receive_queue);
			while(skb && skb != (struct sk_buff *)&s->receive_queue)
			{
				nextsk=skb->next;
				/*
				 *      Do we have file descriptors ?
 				 */
				if(*(int *)(skb->h.filp))
				{
					/* 
					 *	Pull these buffers out of line
					 *	so they will each be freed once
					 *	at the end.
					 */       	
					skb_unlink(skb);
					skb_queue_tail(&hitlist,skb);
				}
				skb=nextsk;
			}
		}
	}

	/*
	 *      Here we are. Hitlist is filled. Die.
	 */

	while ((skb=skb_dequeue(&hitlist))!=NULL) 
	{
		kfree_skb(skb, FREE_READ);
	}

	in_unix_gc=0;
	vfree(stack);
}
