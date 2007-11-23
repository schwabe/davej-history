/*
 *  ioctl.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#include <linux/config.h>

#include <asm/segment.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ncp_fs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/ncp.h>
#include "ncplib_kernel.h"

int
ncp_ioctl (struct inode * inode, struct file * filp,
           unsigned int cmd, unsigned long arg)
{
	int result;
	struct ncp_ioctl_request request;
	struct ncp_fs_info info;
	struct ncp_server *server = NCP_SERVER(inode);

	/*
	 * Binary compatible with 1.3.XX releases.
	 * Take this out in 2.1.0 development series.
	 * <mec@duracef.shout.net> 12 Mar 1996
	 */
	switch(cmd) {
	case _IOR('n', 1, unsigned char *):
	    cmd = NCP_IOC_NCPREQUEST;
	    break;
	case _IOR('u', 1, uid_t):
	    cmd = NCP_IOC_GETMOUNTUID;
	    break;
	case _IO('l', 1):
	    cmd = NCP_IOC_CONN_LOGGED_IN;
	    break;
	case _IOWR('i', 1, unsigned char *):
	    cmd = NCP_IOC_GET_FS_INFO;
	    break;
	}

	switch(cmd) {
	case NCP_IOC_NCPREQUEST:

		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		
		if ((result = verify_area(VERIFY_READ, (char *)arg,
					  sizeof(request))) != 0)
		{
			return result;
		}

		memcpy_fromfs(&request, (struct ncp_ioctl_request *)arg,
			      sizeof(request));

		if (   (request.function > 255)
		    || (request.size >
			NCP_PACKET_SIZE - sizeof(struct ncp_request_header)))
		{
			return -EINVAL;
		}

		if ((result = verify_area(VERIFY_WRITE, (char *)request.data,
					  NCP_PACKET_SIZE)) != 0)
		{
			return result;
		}

		ncp_lock_server(server);

		/* FIXME: We hack around in the server's structures
                   here to be able to use ncp_request */

		server->has_subfunction = 0;
		server->current_size = request.size;
		memcpy_fromfs(server->packet, request.data, request.size);

		ncp_request(server, request.function);

		DPRINTK("ncp_ioctl: copy %d bytes\n",
			server->reply_size);
		memcpy_tofs(request.data, server->packet, server->reply_size);

		ncp_unlock_server(server);

		return server->reply_size;

	case NCP_IOC_CONN_LOGGED_IN:

		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}

		return ncp_conn_logged_in(server);
		
	case NCP_IOC_GET_FS_INFO:

		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		
		if ((result = verify_area(VERIFY_WRITE, (char *)arg,
					  sizeof(info))) != 0)
		{
			return result;
		}

		memcpy_fromfs(&info, (struct ncp_fs_info *)arg,
			      sizeof(info));

		if (info.version != NCP_GET_FS_INFO_VERSION)
		{
			DPRINTK("info.version invalid: %d\n", info.version);
			return -EINVAL;
		}

		info.addr          = server->m.serv_addr;
		info.mounted_uid   = server->m.mounted_uid;
		info.connection    = server->connection;
		info.buffer_size   = server->buffer_size;
		info.volume_number = NCP_ISTRUCT(inode)->volNumber;
		info.directory_id  = NCP_ISTRUCT(inode)->DosDirNum;

		memcpy_tofs((struct ncp_fs_info *)arg, &info, sizeof(info));
		return 0;		

        case NCP_IOC_GETMOUNTUID:

		if (   (permission(inode, MAY_READ) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		
                if ((result = verify_area(VERIFY_WRITE, (uid_t*) arg,
                                          sizeof(uid_t))) != 0)
		{
                        return result;
                }
                put_fs_word(server->m.mounted_uid, (uid_t*) arg);
                return 0;

#if 0
	case NCP_IOC_GETMOUNTUID_INT:
		if (   (permission(inode, MAY_READ) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}

		if ((result = verify_area(VERIFY_WRITE, (unsigned int*)arg,
					sizeof(unsigned int))) != 0)
		{
			return result;
		}
		{
			unsigned int tmp=server->m.mounted_uid;
			put_fs_long(tmp, (unsigned int*) arg);
		}
		return 0;
#endif

#ifdef CONFIG_NCPFS_MOUNT_SUBDIR
	case NCP_IOC_GETROOT:
		{
			struct ncp_setroot_ioctl sr;

			if (   (permission(inode, MAY_READ) != 0)
			    && (current->uid != server->m.mounted_uid))
			{
				return -EACCES;
			}
			if (server->m.mounted_vol[0]) {
				sr.volNumber = server->root.finfo.i.volNumber;
				sr.dirEntNum = server->root.finfo.i.dirEntNum;
				sr.namespace = server->name_space[sr.volNumber];
			} else {
				sr.volNumber = -1;
				sr.namespace = 0;
				sr.dirEntNum = 0;
			}
			if ((result = verify_area(VERIFY_WRITE, 
						  (struct ncp_setroot_ioctl*)arg,
						  sizeof(sr))) != 0)
			{
				return result;
			}
			memcpy_tofs((struct ncp_setroot_ioctl*)arg, 
				    &sr, sizeof(sr));
			return 0;
		}
	case NCP_IOC_SETROOT:
		{
			struct ncp_setroot_ioctl sr;

			if (   (permission(inode, MAY_WRITE) != 0)
			    && (current->uid != server->m.mounted_uid))
			{
				return -EACCES;
			}
			if ((result = verify_area(VERIFY_READ, 
						  (struct ncp_setroot_ioctl*)arg,
						  sizeof(sr))) != 0)
			{
				return result;
			}
			memcpy_fromfs(&sr, (struct ncp_setroot_ioctl*)arg, sizeof(sr));
			if (sr.volNumber < 0) {
				server->m.mounted_vol[0] = 0;
				server->root.finfo.i.volNumber = 0;
				server->root.finfo.i.dirEntNum = 0;
			} else if (sr.volNumber >= NCP_NUMBER_OF_VOLUMES) {
				return -EINVAL;
			} else {
				if (ncp_mount_subdir(server, sr.volNumber, sr.namespace, sr.dirEntNum)) {
					return -ENOENT;
				}
			}
			return 0;
		}
#endif	/* CONFIG_NCPFS_MOUNT_SUBDIR */

#ifdef CONFIG_NCPFS_PACKET_SIGNING	
	case NCP_IOC_SIGN_INIT:
		if ((permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		if ((result = verify_area(VERIFY_READ, (struct ncp_sign_init*)arg, 
					  sizeof(struct ncp_sign_init))) != 0)
		{
			return result;
		}
		if (server->sign_active)
		{
			return -EINVAL;
		}
		if (server->sign_wanted)
		{
			struct ncp_sign_init sign;

			memcpy_fromfs(&sign, (struct ncp_sign_init *) arg,
			      sizeof(sign));
			memcpy(server->sign_root,sign.sign_root,8);
			memcpy(server->sign_last,sign.sign_last,16);
			server->sign_active = 1;
		}
		/* ignore when signatures not wanted */
		return 0;		
	
        case NCP_IOC_SIGN_WANTED:
		if (   (permission(inode, MAY_READ) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
                if ((result = verify_area(VERIFY_WRITE, (int*) arg,
                                          sizeof(int))) != 0)
		{
                        return result;
		}
		/* Should not it be put_fs_long? Vandrove@vc.cvut.cz */
               	put_fs_word(server->sign_wanted, (int*) arg);
               	return 0;

	case NCP_IOC_SET_SIGN_WANTED:
		{
			int newstate;

			if (   (permission(inode, MAY_WRITE) != 0)
			    && (current->uid != server->m.mounted_uid))
			{
				return -EACCES;
			}
			if ((result = verify_area(VERIFY_READ, (int*) arg,
						  sizeof(int))) != 0)
			{
				return result;
			}
			/* get only low 8 bits... */
			newstate = get_fs_byte((unsigned char*)arg);
			if (server->sign_active) {
				/* cannot turn signatures OFF when active */
				if (!newstate) return -EINVAL;
			} else {
				server->sign_wanted = newstate != 0;
			}
			return 0;
		}

#endif /* CONFIG_NCPFS_PACKET_SIGNING */

#ifdef CONFIG_NCPFS_IOCTL_LOCKING
	case NCP_IOC_LOCKUNLOCK:
		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		{
			struct ncp_lock_ioctl	 rqdata;
			struct nw_file_info	*finfo;
			int result;

			if ((result = verify_area(VERIFY_READ, 
					(struct ncp_lock_ioctl*)arg, 
					sizeof(rqdata))) != 0)
			{
				return result;
			}
			memcpy_fromfs(&rqdata, (struct ncp_lock_ioctl*)arg,
				sizeof(rqdata));
			if (rqdata.origin != 0)
				return -EINVAL;
			/* check for cmd */
			switch (rqdata.cmd) {
				case NCP_LOCK_EX:
				case NCP_LOCK_SH:
						if (rqdata.timeout == 0)
							rqdata.timeout = NCP_LOCK_DEFAULT_TIMEOUT;
						else if (rqdata.timeout > NCP_LOCK_MAX_TIMEOUT)
							rqdata.timeout = NCP_LOCK_MAX_TIMEOUT;
						break;
				case NCP_LOCK_LOG:
						rqdata.timeout = NCP_LOCK_DEFAULT_TIMEOUT;	/* has no effect */
				case NCP_LOCK_CLEAR:
						break;
				default:
						return -EINVAL;
			}
			if ((result = ncp_make_open(inode, O_RDWR)) != 0)
			{
				return result;
			}
			if (!ncp_conn_valid(server))
			{
				return -EIO;
			}
			if (!S_ISREG(inode->i_mode))
			{
				return -EISDIR;
			}
			finfo=NCP_FINFO(inode);
			if (!finfo->opened)
			{
				return -EBADFD;
			}
			if (rqdata.cmd == NCP_LOCK_CLEAR)
			{
				result = ncp_ClearPhysicalRecord(NCP_SERVER(inode),
							finfo->file_handle, 
							rqdata.offset,
							rqdata.length);
				if (result > 0) result = 0;	/* no such lock */
			}
			else
			{
				int lockcmd;

				switch (rqdata.cmd)
				{
					case NCP_LOCK_EX:  lockcmd=1; break;
					case NCP_LOCK_SH:  lockcmd=3; break;
					default:	   lockcmd=0; break;
				}
				result = ncp_LogPhysicalRecord(NCP_SERVER(inode),
							finfo->file_handle,
							lockcmd,
							rqdata.offset,
							rqdata.length,
							rqdata.timeout);
				if (result > 0) result = -EAGAIN;
			}
			return result;
		}
#endif	/* CONFIG_NCPFS_IOCTL_LOCKING */

	default:
		return -EINVAL;
	}
	
	return -EINVAL;
}
