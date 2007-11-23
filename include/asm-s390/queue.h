/*
 *  include/asm-s390/queue.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  A little set of queue utilies.
 */
#include <linux/stddef.h>

typedef struct queue
{
	struct queue *next;	
} queue;

typedef struct
{
	queue *head;
	queue *tail;
} qheader;

static __inline__ void init_queue(qheader *qhead)
{
	memset(qhead,0,sizeof(*qhead));
}

static __inline__ void enqueue_tail(qheader *qhead,queue *member)
{
	queue *tail=qhead->tail;
	member->next=NULL;
	
	if(member)
	{
		if(tail)
			tail->next=member;
		else
			
			qhead->head=member;
		qhead->tail=member;
		member->next=NULL;
	}
} 

static __inline__ queue *dequeue_head(qheader *qhead)
{
	queue *head=qhead->head,*next_head;

	if(head)
	{
		next_head=head->next;
		qhead->head=next_head;
	        if(!next_head)
			qhead->tail=NULL;
	}
	return(head);
}







