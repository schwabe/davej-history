/*
 *  binfmt_misc.c
 *
 *  Copyright (C) 1997 Richard Günther
 *
 *  binfmt_misc detects binaries via a magic or filename extension and invokes
 *  a specified wrapper. This should obsolete binfmt_java, binfmt_em86 and
 *  binfmt_mz.
 *
 *  1997-04-25 first version
 *  [...]
 *  1997-05-19 cleanup
 *  1997-06-26 hpa: pass the real filename rather than argv[0]
 *  1997-06-30 minor cleanup
 *  1997-08-09 removed extension stripping, locking cleanup
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/binfmts.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/uaccess.h>
#include <asm/spinlock.h>

/*
 * We should make this work with a "stub-only" /proc,
 * which would just not be able to be configured.
 * Right now the /proc-fs support is too black and white,
 * though, so just remind people that this should be
 * fixed..
 */
#ifndef CONFIG_PROC_FS
#error You really need /proc support for binfmt_misc. Please reconfigure!
#endif

enum {
	VERBOSE_STATUS = 1 /* define as zero to save 400 bytes kernel memory */
};

typedef struct binfmt_entry {
	struct binfmt_entry *next;
	long id;
	long flags;			/* type, status, etc. */
	int offset;			/* offset of magic */
	int size;			/* size of magic/mask */
	char *magic;			/* magic or filename extension */
	char *mask;			/* mask, NULL for exact match */
	char *interpreter;		/* filename of interpreter */
	char *name;
	struct proc_dir_entry *proc_dir;
} Node;

enum { Enabled, Magic };

static int load_misc_binary(struct linux_binprm *bprm, struct pt_regs *regs);
static void entry_proc_cleanup(Node *e);
static int entry_proc_setup(Node *e);

static struct linux_binfmt misc_format = {
	module:		THIS_MODULE,
	load_binary:	load_misc_binary,
};

static struct proc_dir_entry *bm_dir = NULL;

static Node *entries = NULL;
static int free_id = 1;
static int enabled = 1;

static rwlock_t entries_lock __attribute__((unused)) = RW_LOCK_UNLOCKED;


/*
 * Unregister one entry
 */
static void clear_entry(int id)
{
	Node **ep, *e;

	write_lock(&entries_lock);
	ep = &entries;
	while (*ep && ((*ep)->id != id))
		ep = &((*ep)->next);
	if ((e = *ep))
		*ep = e->next;
	write_unlock(&entries_lock);

	if (e) {
		entry_proc_cleanup(e);
		kfree(e);
	}
}

/*
 * Clear all registered binary formats
 */
static void clear_entries(void)
{
	Node *e, *n;

	write_lock(&entries_lock);
	n = entries;
	entries = NULL;
	write_unlock(&entries_lock);

	while ((e = n)) {
		n = e->next;
		entry_proc_cleanup(e);
		kfree(e);
	}
}

/*
 * Find entry through id and lock it
 */
static Node *get_entry(int id)
{
	Node *e;

	read_lock(&entries_lock);
	e = entries;
	while (e && (e->id != id))
		e = e->next;
	if (!e)
		read_unlock(&entries_lock);
	return e;
}

/*
 * unlock entry
 */
static inline void put_entry(Node *e)
{
	if (e)
		read_unlock(&entries_lock);
}


/* 
 * Check if we support the binfmt
 * if we do, return the node, else NULL
 * locking is done in load_misc_binary
 */
static Node *check_file(struct linux_binprm *bprm)
{
	Node *e;
	char *p = strrchr(bprm->filename, '.');
	int j;

	e = entries;
	while (e) {
		if (test_bit(Enabled, &e->flags)) {
			if (!test_bit(Magic, &e->flags)) {
				if (p && !strcmp(e->magic, p + 1))
					return e;
			} else {
				j = 0;
				while ((j < e->size) &&
				  !((bprm->buf[e->offset + j] ^ e->magic[j])
				   & (e->mask ? e->mask[j] : 0xff)))
					j++;
				if (j == e->size)
					return e;
			}
		}
		e = e->next;
	};
	return NULL;
}

/*
 * the loader itself
 */
static int load_misc_binary(struct linux_binprm *bprm, struct pt_regs *regs)
{
	Node *fmt;
	struct dentry * dentry;
	char iname[128];
	char *iname_addr = iname;
	int retval;

	retval = -ENOEXEC;
	if (!enabled)
		goto _ret;

	/* to keep locking time low, we copy the interpreter string */
	read_lock(&entries_lock);
	fmt = check_file(bprm);
	if (fmt) {
		strncpy(iname, fmt->interpreter, 127);
		iname[127] = '\0';
	}
	read_unlock(&entries_lock);
	if (!fmt)
		goto _ret;

	dput(bprm->dentry);
	bprm->dentry = NULL;

	/* Build args for interpreter */
	remove_arg_zero(bprm);
	bprm->p = copy_strings(1, &bprm->filename, bprm->page, bprm->p, 2);
	bprm->argc++;
	bprm->p = copy_strings(1, &iname_addr, bprm->page, bprm->p, 2);
	bprm->argc++;
	retval = (long)bprm->p;
	if ((long)bprm->p < 0)
		goto _ret;
	bprm->filename = iname;	/* for binfmt_script */

	dentry = open_namei(iname, 0, 0);
	retval = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto _ret;
	bprm->dentry = dentry;

	retval = prepare_binprm(bprm);
	if (retval >= 0)
		retval = search_binary_handler(bprm, regs);
_ret:
	return retval;
}

/*
 * parses and copies one argument enclosed in del from *sp to *dp,
 * recognising the \x special.
 * returns pointer to the copied argument or NULL in case of an
 * error (and sets err) or null argument length.
 */
static char *scanarg(char *s, char del)
{
	char c;

	while ((c = *s++) != del) {
		if (c == '\\' && *s == 'x') {
			s++;
			if (!isxdigit(*s++))
				return NULL;
			if (!isxdigit(*s++))
				return NULL;
		}
	}
	return s;
}

static int unquote(char *from)
{
	char c = 0, *s = from, *p = from;

	while ((c = *s++) != '\0') {
		if (c == '\\' && *s == 'x') {
			s++;
			c = toupper(*s++);
			*p = (c - (isdigit(c) ? '0' : 'A' - 10)) << 4;
			c = toupper(*s++);
			*p++ |= c - (isdigit(c) ? '0' : 'A' - 10);
			continue;
		}
		*p++ = c;
	}
	return p - from;
}

/*
 * This registers a new binary format, it recognises the syntax
 * ':name:type:offset:magic:mask:interpreter:'
 * where the ':' is the IFS, that can be chosen with the first char
 */
static Node *create_entry(const char *buffer, size_t count)
{
	Node *e;
	int memsize, err;
	char *buf, *p;
	char del;

	/* some sanity checks */
	err = -EINVAL;
	if ((count < 11) || (count > 256))
		goto out;

	err = -ENOMEM;
	memsize = sizeof(Node) + count + 8;
	e = (Node *) kmalloc(memsize, GFP_USER);
	if (!e)
		goto out;

	p = buf = (char *)e + sizeof(Node);

	memset(e, 0, sizeof(Node));
	if (copy_from_user(buf, buffer, count))
		goto Efault;

	del = *p++;	/* delimeter */

	memset(buf+count, del, 8);

	e->name = p;
	p = strchr(p, del);
	if (!p)
		goto Einval;
	*p++ = '\0';
	if (!e->name[0] ||
	    !strcmp(e->name, ".") ||
	    !strcmp(e->name, "..") ||
	    strchr(e->name, '/'))
		goto Einval;
	switch (*p++) {
		case 'E': e->flags = 1<<Enabled; break;
		case 'M': e->flags = (1<<Enabled) | (1<<Magic); break;
		default: goto Einval;
	}
	if (*p++ != del)
		goto Einval;
	if (test_bit(Magic, &e->flags)) {
		char *s = strchr(p, del);
		if (!s)
			goto Einval;
		*s++ = '\0';
		e->offset = simple_strtoul(p, &p, 10);
		if (*p++)
			goto Einval;
		e->magic = p;
		p = scanarg(p, del);
		if (!p)
			goto Einval;
		p[-1] = '\0';
		if (!e->magic[0])
			goto Einval;
		e->mask = p;
		p = scanarg(p, del);
		if (!p)
			goto Einval;
		p[-1] = '\0';
		if (!e->mask[0])
			e->mask = NULL;
		e->size = unquote(e->magic);
		if (e->mask && unquote(e->mask) != e->size)
			goto Einval;
		if (e->size + e->offset > 128)
			goto Einval;
	} else {
		p = strchr(p, del);
		if (!p)
			goto Einval;
		*p++ = '\0';
		e->magic = p;
		p = strchr(p, del);
		if (!p)
			goto Einval;
		*p++ = '\0';
		if (!e->magic[0] || strchr(e->magic, '/'))
			goto Einval;
		p = strchr(p, del);
		if (!p)
			goto Einval;
		*p++ = '\0';
	}
	e->interpreter = p;
	p = strchr(p, del);
	if (!p)
		goto Einval;
	*p++ = '\0';
	if (!e->interpreter[0])
		goto Einval;

	if (*p == '\n')
		p++;
	if (p != buf + count)
		goto Einval;
	return e;

out:
	return ERR_PTR(err);

Efault:
	kfree(e);
	return ERR_PTR(-EFAULT);
Einval:
	kfree(e);
	return ERR_PTR(-EINVAL);
}

/*
 * Set status of entry/binfmt_misc:
 * '1' enables, '0' disables and '-1' clears entry/binfmt_misc
 */
static int parse_command(const char *buffer, size_t count)
{
	char s[4];

	if (!count)
		return 0;
	if (count > 3)
		return -EINVAL;
	if (copy_from_user(s, buffer, count))
		return -EFAULT;
	if (s[count-1] == '\n')
		count--;
	if (count == 1 && s[0] == '0')
		return 1;
	if (count == 1 && s[0] == '1')
		return 2;
	if (count == 2 && s[0] == '-' && s[1] == '1')
		return 3;
	return -EINVAL;
}

static void entry_status(Node *e, char *page)
{
	char *dp;
	char *status = "disabled";

	if (test_bit(Enabled, &e->flags))
		status = "enabled";

	if (!VERBOSE_STATUS) {
		sprintf(page, "%s\n", status);
		return;
	}

	sprintf(page, "%s\ninterpreter %s\n", status, e->interpreter);
	dp = page + strlen(page);
	if (!test_bit(Magic, &e->flags)) {
		sprintf(dp, "extension .%s\n", e->magic);
	} else {
		int i;

		sprintf(dp, "offset %i\nmagic ", e->offset);
		dp = page + strlen(page);
		for (i = 0; i < e->size; i++) {
			sprintf(dp, "%02x", 0xff & (int) (e->magic[i]));
			dp += 2;
		}
		if (e->mask) {
			sprintf(dp, "\nmask ");
			dp += 6;
			for (i = 0; i < e->size; i++) {
				sprintf(dp, "%02x", 0xff & (int) (e->mask[i]));
				dp += 2;
			}
		}
		*dp++ = '\n';
		*dp = '\0';
	}
}

static int proc_write_register(struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	Node *e = create_entry(buffer, count);
	int err;

	if (IS_ERR(e))
		return PTR_ERR(e);
	
	if (entry_proc_setup(e))
		goto free_err;

	e->id = free_id++;
	write_lock(&entries_lock);
	e->next = entries;
	entries = e;
	write_unlock(&entries_lock);

	err = count;
_err:
	return err;
free_err:
	kfree(e);
	err = -EINVAL;
	goto _err;
}

/*
 * Get status of entry/binfmt_misc
 * FIXME? should an entry be marked disabled if binfmt_misc is disabled though
 *        entry is enabled?
 */
static int proc_read_status(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	Node *e;
	int elen;

	if (!data) {
		sprintf(page, "%s\n", (enabled ? "enabled" : "disabled"));
	} else {
		if (!(e = get_entry((long) data)))
			return -ENOENT;
		entry_status(e, page);
		put_entry(e);
	}

	elen = strlen(page) - off;
	if (elen < 0)
		elen = 0;
	*eof = (elen <= count) ? 1 : 0;
	*start = page + off;
	return elen;
}

/*
 * Set status of entry/binfmt_misc:
 * '1' enables, '0' disables and '-1' clears entry/binfmt_misc
 */
static int proc_write_status(struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	Node *e;
	int res = parse_command(buffer, count);

	switch(res) {
		case 1: if (data) {
				if ((e = get_entry((long) data)))
					clear_bit(Enabled, &e->flags);
				put_entry(e);
			} else {
				enabled = 0;
			}
			break;
		case 2: if (data) {
				if ((e = get_entry((long) data)))
					set_bit(Enabled, &e->flags);
				put_entry(e);
			} else {
				enabled = 1;
			}
			break;
		case 3: if (data)
				clear_entry((long) data);
			else
				clear_entries();
			break;
		default: return res;
	}
	return count;
}

/*
 * Remove the /proc-dir entries of one binfmt
 */
static void entry_proc_cleanup(Node *e)
{
	remove_proc_entry(e->name, bm_dir);
}

/*
 * Create the /proc-dir entry for binfmt
 */
static int entry_proc_setup(Node *e)
{
	if (!(e->proc_dir = create_proc_entry(e->name,
			 	S_IFREG | S_IRUGO | S_IWUSR, bm_dir)))
	{
		printk(KERN_WARNING "Unable to create /proc entry.\n");
		return -ENOENT;
	}
	e->proc_dir->data = (void *) (e->id);
	e->proc_dir->read_proc = proc_read_status;
	e->proc_dir->write_proc = proc_write_status;
	return 0;
}

#ifdef MODULE
/*
 * This is called as the fill_inode function when an inode
 * is going into (fill = 1) or out of service (fill = 0).
 * We use it here to manage the module use counts.
 *
 * Note: only the top-level directory needs to do this; if
 * a lower level is referenced, the parent will be as well.
 */
static void bm_modcount(struct inode *inode, int fill)
{
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
}
#endif

int __init init_misc_binfmt(void)
{
	int error = -ENOENT;
	struct proc_dir_entry *status = NULL, *reg;

	bm_dir = create_proc_entry("sys/fs/binfmt_misc", S_IFDIR, NULL);
	if (!bm_dir)
		goto out;
#ifdef MODULE
	bm_dir->fill_inode = bm_modcount;
#endif

	status = create_proc_entry("status", S_IFREG | S_IRUGO | S_IWUSR,
					bm_dir);
	if (!status)
		goto cleanup_bm;
	status->read_proc = proc_read_status;
	status->write_proc = proc_write_status;

	reg = create_proc_entry("register", S_IFREG | S_IWUSR, bm_dir);
	if (!reg)
		goto cleanup_status;
	reg->write_proc = proc_write_register;

	error = register_binfmt(&misc_format);
out:
	return error;

cleanup_status:
	remove_proc_entry("status", bm_dir);
cleanup_bm:
	remove_proc_entry("sys/fs/binfmt_misc", NULL);
	goto out;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;
int init_module(void)
{
	return init_misc_binfmt();
}

void cleanup_module(void)
{
	unregister_binfmt(&misc_format);
	remove_proc_entry("register", bm_dir);
	remove_proc_entry("status", bm_dir);
	clear_entries();
	remove_proc_entry("sys/fs/binfmt_misc", NULL);
}
#endif
