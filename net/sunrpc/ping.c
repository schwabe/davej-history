/*
 * linux/net/sunrpc/ping.c
 *
 * Ping routing.
 *
 * Copyright (C) 2000, Trond Myklebust <trond.myklebust@fys.uio.no>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/uio.h>
#include <linux/in.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/sched.h>


#define RPC_SLACK_SPACE		512	/* total overkill */
#define RPC_PING_DELAY		(15*HZ)

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_XPRT
#endif

static void ping_call_reserve(struct rpc_task *);
static void ping_call_allocate(struct rpc_task *);
static void ping_call_encode(struct rpc_task *);
static void ping_call_transmit(struct rpc_task *);
static void ping_call_receive(struct rpc_task *);
static void ping_call_exit(struct rpc_task *);


static void
ping_call_reserve(struct rpc_task *task)
{
	dprintk("RPC: %4d, ping_call_reserve\n", task->tk_pid);
	task->tk_status = 0;
	task->tk_action  = ping_call_allocate;
	task->tk_timeout = task->tk_client->cl_timeout.to_resrvval;
	xprt_ping_reserve(task);
}

static void
ping_call_allocate(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;
	unsigned int	bufsiz;

	dprintk("RPC: %4d, ping_call_allocate (status %d)\n",
		task->tk_pid, task->tk_status);

	task->tk_action = ping_call_exit;
	if (task->tk_status < 0)
		return;

	bufsiz = rpcproc_bufsiz(clnt, task->tk_msg.rpc_proc) + RPC_SLACK_SPACE;
	if (!(task->tk_buffer = rpc_malloc(task, bufsiz << 1))) {
		task->tk_status = -ENOMEM;
		return;
	}
	req->rq_svec[0].iov_base = (void *)task->tk_buffer;
	req->rq_svec[0].iov_len	 = bufsiz;
	req->rq_slen		 = 0;
	req->rq_snr		 = 1;
	req->rq_rvec[0].iov_base = (void *)((char *)task->tk_buffer + bufsiz);
	req->rq_rvec[0].iov_len	 = bufsiz;
	req->rq_rlen		 = bufsiz;
	req->rq_rnr		 = 1;
	task->tk_action		 = ping_call_encode;
}

static void
ping_call_encode(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	u32		*p;

	dprintk("RPC: %4d, ping_call_encode (status %d)\n",
		task->tk_pid, task->tk_status);

	if (task->tk_status < 0) {
		task->tk_action = ping_call_exit;
		return;
	}
	p = rpc_call_header(task);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	task->tk_action = ping_call_transmit;
}

static void
ping_call_transmit(struct rpc_task *task)
{
	dprintk("RPC: %4d, ping_call_transmit\n", task->tk_pid);
	task->tk_action = ping_call_receive;
	xprt_transmit(task);
}

static void
ping_call_receive(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_xprt	*xprt = clnt->cl_xprt;
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_timeout *to = &req->rq_timeout;
	u32 *p;

	dprintk("RPC: %4d, ping_call_receive (status %d)\n",
		task->tk_pid, task->tk_status);

	if (task->tk_status >= 0)
		p = rpc_call_verify(task);

	task->tk_action = ping_call_exit;

	if (task->tk_status >= 0 || task->tk_status == -EACCES) {
		task->tk_status = 0;
		if (xprt_norespond(xprt)) {
			if (clnt->cl_chatty)
				printk(KERN_NOTICE "%s: server %s OK\n",
				       clnt->cl_protname, clnt->cl_server);
			xprt_clear_norespond(xprt);
		}
		return;
	}

	switch (task->tk_status) {
	case -ENOTCONN:
		break;
	case -ENOMEM:
	case -EAGAIN:
	case -ECONNREFUSED:
	case -ETIMEDOUT:
		if (!xprt_adjust_timeout(to)) {
			task->tk_status = 0;
			task->tk_action = ping_call_transmit;
			break;
		}
	default:
		if (clnt->cl_softrtry) {
			task->tk_status = -EIO;
			break;
		}
		if (clnt->cl_chatty) {
			if (!xprt_test_and_set_norespond(xprt)) {
				printk(KERN_NOTICE
				       "%s: server %s is not responding\n",
				       clnt->cl_protname, clnt->cl_server);
			} else {
				printk(KERN_NOTICE
				       "%s: server %s still not responding\n",
				       clnt->cl_protname, clnt->cl_server);
			}
		}
		rpc_delay(task, RPC_PING_DELAY);
	}
}

static void
ping_call_exit(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	dprintk("RPC: %4d, ping_call_exit (status %d)\n",
		task->tk_pid, task->tk_status);

	task->tk_action = NULL;
	xprt_ping_release(task);

	/* Sigh. rpc_delay() clears task->tk_status */
	if (task->tk_status == 0 && xprt_norespond(xprt))
		task->tk_status = -ETIMEDOUT;

	xprt_clear_pinging(xprt);
	rpc_wake_up_status(&xprt->pingwait, task->tk_status);
}

void
rpc_ping(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_xprt	*xprt = clnt->cl_xprt;
	struct rpc_task	*child;
	struct rpc_message msg = {0, NULL, NULL, NULL};

	dprintk("RPC: %4d, rpc_ping\n", task->tk_pid);

 again:
	if (xprt_test_and_set_pinging(xprt)) {
		rpc_sleep_on(&xprt->pingwait, task, NULL, 0);
		if (!xprt_pinging(xprt)) {
			rpc_wake_up_task(task);
			goto again;
		}
		dprintk("RPC: %4d, rpc_ping, waiting on completion\n",
			task->tk_pid);
		return;
	}

	child = rpc_new_child(clnt, task);
	if (!child) {
		dprintk("RPC: %4d, rpc_ping, failed to create child process\n",
			task->tk_pid);
		xprt_clear_pinging(xprt);
		rpc_wake_up_status(&xprt->pingwait, -ENOMEM);
		task->tk_status = -ENOMEM;
		return;
	}
	rpc_call_setup(child, &msg, 0);
	child->tk_action = ping_call_reserve;

	dprintk("RPC: %4d, rpc_ping, running child process %4d\n",
		task->tk_pid, child->tk_pid);
	rpc_run_child(task, child, NULL);
}
