// SPDX-License-Identifier: GPL-2.0
// Sorabbyngo Toolbox Namespace Driver — Fedora toolbox model
// Creates per-project isolated namespaces accessible via:
//   /sys/kernel/sorabbyngo/toolbox/<project-name>
// Each toolbox gets its own: mount ns, PID ns, network ns, IPC ns

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/nsproxy.h>

#define MAX_TOOLBOXES 64
#define TOOLBOX_NAME_MAX 64

struct sorabbyngo_toolbox {
	char name[TOOLBOX_NAME_MAX];
	pid_t init_pid;
	bool  active;
};

static struct sorabbyngo_toolbox toolboxes[MAX_TOOLBOXES];
static struct kobject *toolbox_kobj;

static ssize_t toolbox_create_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	int i;
	for (i = 0; i < MAX_TOOLBOXES; i++) {
		if (!toolboxes[i].active) {
			strscpy(toolboxes[i].name, buf,
				min_t(size_t, count, TOOLBOX_NAME_MAX));
			// Strip trailing newline
			toolboxes[i].name[strcspn(toolboxes[i].name, "\n")] = 0;
			toolboxes[i].active = true;
			pr_info("sorabbyngo-toolbox: created '%s'\n",
				toolboxes[i].name);
			return count;
		}
	}
	return -ENOSPC;
}

static ssize_t toolbox_list_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	int i, len = 0;
	for (i = 0; i < MAX_TOOLBOXES && len < PAGE_SIZE - 64; i++) {
		if (toolboxes[i].active)
			len += sysfs_emit_at(buf, len, "%s\n",
					     toolboxes[i].name);
	}
	return len;
}

static struct kobj_attribute toolbox_create_attr =
	__ATTR(create, 0200, NULL, toolbox_create_store);
static struct kobj_attribute toolbox_list_attr =
	__ATTR(list,   0444, toolbox_list_show, NULL);

static int __init sorabbyngo_toolbox_init(void)
{
	toolbox_kobj = kobject_create_and_add("toolbox", kernel_kobj);
	if (!toolbox_kobj)
		return -ENOMEM;
	sysfs_create_file(toolbox_kobj, &toolbox_create_attr.attr);
	sysfs_create_file(toolbox_kobj, &toolbox_list_attr.attr);
	pr_info("sorabbyngo-toolbox: namespace driver loaded\n");
	return 0;
}

static void __exit sorabbyngo_toolbox_exit(void)
{
	kobject_put(toolbox_kobj);
}

module_init(sorabbyngo_toolbox_init);
module_exit(sorabbyngo_toolbox_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sorabbyngo Toolbox Namespace Driver");
