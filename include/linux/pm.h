#ifndef __LINUX_PM_H
#define __LINUX_PM_H


typedef int pm_request_t;

struct pm_dev {
	void *data;
	struct pm_dev *next;
	int (*event)(struct pm_dev *, pm_request_t, void *);
};

#define PM_SUSPEND	1
#define PM_RESUME	2

#ifdef CONFIG_APM

#include <linux/apm_bios.h>

#define PM_PCI_DEV	0
#define PM_PCI_ID(a)	a

static struct pm_dev *pm_dev = NULL;

static int handle_apm_event(apm_event_t apm_event)
{
	struct pm_dev *list = pm_dev;
	pm_request_t action;

	switch (apm_event) {

		case APM_SYS_SUSPEND:
		case APM_CRITICAL_SUSPEND:
		case APM_USER_SUSPEND:
			action = PM_SUSPEND;
			break;
		case APM_NORMAL_RESUME:
		case APM_CRITICAL_RESUME:
		case APM_STANDBY_RESUME:
			action = PM_RESUME;
			break;
		default:
			return 0;
	}

	while (list) {
		list->event(list, action, list->data);
		list = list->next;
	}

	return 0;
}

static struct pm_dev *pm_register(int a, void *b, int (*event)(struct pm_dev *, pm_request_t, void *))
{
	struct pm_dev *list;
	
	list = kmalloc(sizeof(struct pm_dev), GFP_KERNEL);
	list->event = event;
	list->next = pm_dev;
	pm_dev = list;

	apm_register_callback(handle_apm_event);

	return list;
}

static void pm_unregister_all(int (*event)(struct pm_dev *, pm_request_t, void *))
{
	struct pm_dev *list;

	while (pm_dev) {
		list = pm_dev;
		pm_dev = pm_dev->next;
		kfree(list);
	}

	apm_unregister_callback(handle_apm_event);
}

#else

#define pm_register(a,b,c)	0
#define pm_unregister_all(a)	do {} while (0)

#endif

#endif
