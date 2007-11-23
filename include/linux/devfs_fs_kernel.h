typedef int* devfs_handle_t;

#define devfs_register(a,b,c,d,e,f,g,h,i,j,k)	NULL
#define devfs_unregister(a)
#define devfs_mk_dir(a,b,c,d)			NULL
#define devfs_register_chrdev(a,b,c)		register_chrdev(a,b,c)
#define devfs_unregister_chrdev(a,b)		unregister_chrdev(a,b)
#define tty_register_devfs(driver,flags,minor)
#define tty_unregister_devfs(driver,minor)
