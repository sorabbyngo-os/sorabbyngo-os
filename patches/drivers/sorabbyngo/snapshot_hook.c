// SPDX-License-Identifier: GPL-2.0
// Sorabbyngo Atomic Snapshot Hook
// Exposes /sys/kernel/sorabbyngo/snapshot/{create,rollback,list}
// Called by self-healing daemon and package manager before installs

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/time.h>
#include <linux/rtc.h>

#define MAX_SNAPSHOTS 32
#define SNAPSHOT_NAME_MAX 128

struct sorabbyngo_snapshot {
	char  name[SNAPSHOT_NAME_MAX];
	u64   created_ns;
	bool  valid;
};

static struct sorabbyngo_snapshot snapshots[MAX_SNAPSHOTS];
static int snapshot_count = 0;
static struct kobject *snapshot_kobj;

static ssize_t snapshot_create_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	if (snapshot_count >= MAX_SNAPSHOTS)
		return -ENOSPC;
	strscpy(snapshots[snapshot_count].name, buf,
		min_t(size_t, count, SNAPSHOT_NAME_MAX));
	snapshots[snapshot_count].name[
		strcspn(snapshots[snapshot_count].name, "\n")] = 0;
	snapshots[snapshot_count].created_ns = ktime_get_ns();
	snapshots[snapshot_count].valid = true;
	pr_info("sorabbyngo-snapshot: created '%s'\n",
		snapshots[snapshot_count].name);
	snapshot_count++;
	return count;
}

static ssize_t snapshot_list_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	int i, len = 0;
	for (i = 0; i < snapshot_count && len < PAGE_SIZE - 128; i++) {
		if (snapshots[i].valid)
			len += sysfs_emit_at(buf, len, "[%d] %s (%llu)\n",
					     i, snapshots[i].name,
					     snapshots[i].created_ns);
	}
	return len;
}

static ssize_t snapshot_rollback_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t count)
{
	// Signal self-healing daemon via a kernel event
	// Full btrfs subvolume rollback handled in userspace by self-healing
	pr_info("sorabbyngo-snapshot: rollback requested: %s\n", buf);
	kobject_uevent(kobj, KOBJ_CHANGE);
	return count;
}

static struct kobj_attribute snap_create_attr =
	__ATTR(create,   0200, NULL,               snapshot_create_store);
static struct kobj_attribute snap_list_attr =
	__ATTR(list,     0444, snapshot_list_show,  NULL);
static struct kobj_attribute snap_rollback_attr =
	__ATTR(rollback, 0200, NULL,               snapshot_rollback_store);

static int __init sorabbyngo_snapshot_init(void)
{
	snapshot_kobj = kobject_create_and_add("snapshot", kernel_kobj);
	if (!snapshot_kobj)
		return -ENOMEM;
	sysfs_create_file(snapshot_kobj, &snap_create_attr.attr);
	sysfs_create_file(snapshot_kobj, &snap_list_attr.attr);
	sysfs_create_file(snapshot_kobj, &snap_rollback_attr.attr);
	pr_info("sorabbyngo-snapshot: hook loaded\n");
	return 0;
}

static void __exit sorabbyngo_snapshot_exit(void)
{
	kobject_put(snapshot_kobj);
}

module_init(sorabbyngo_snapshot_init);
module_exit(sorabbyngo_snapshot_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sorabbyngo Atomic Snapshot Hook");
