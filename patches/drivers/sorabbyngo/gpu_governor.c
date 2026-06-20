// SPDX-License-Identifier: GPL-2.0
// Sorabbyngo GPU Governor — Pop!_OS-inspired day-one GPU support
// Detects Nvidia/AMD at boot, sets mode via domain-mode manager
// Modes: exclusive (gaming) | compute (AI) | shared (dev) | restricted (security)

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

#define SORABBYNGO_GPU_SHARED     0
#define SORABBYNGO_GPU_EXCLUSIVE  1
#define SORABBYNGO_GPU_COMPUTE    2
#define SORABBYNGO_GPU_RESTRICTED 3

static int gpu_mode = SORABBYNGO_GPU_SHARED;
static struct kobject *gpu_kobj;

// PCI IDs for Nvidia and AMD GPUs
static const struct pci_device_id nvidia_ids[] = {
	{ PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_DISPLAY_VGA << 8, 0xffff00, 0 },
	{ 0, }
};

static const struct pci_device_id amd_ids[] = {
	{ PCI_VENDOR_ID_ATI, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_DISPLAY_VGA << 8, 0xffff00, 0 },
	{ 0, }
};

static ssize_t gpu_mode_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	const char *names[] = { "shared", "exclusive", "compute", "restricted" };
	return sysfs_emit(buf, "%d (%s)\n", gpu_mode, names[gpu_mode]);
}

static ssize_t gpu_mode_store(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      const char *buf, size_t count)
{
	int val;
	if (kstrtoint(buf, 10, &val) || val < 0 || val > 3)
		return -EINVAL;
	gpu_mode = val;
	pr_info("sorabbyngo-gpu: mode -> %d\n", val);
	return count;
}

static struct kobj_attribute gpu_mode_attr =
	__ATTR(gpu_mode, 0644, gpu_mode_show, gpu_mode_store);

static int __init sorabbyngo_gpu_init(void)
{
	gpu_kobj = kobject_create_and_add("sorabbyngo_gpu", kernel_kobj);
	if (!gpu_kobj)
		return -ENOMEM;
	sysfs_create_file(gpu_kobj, &gpu_mode_attr.attr);
	pr_info("sorabbyngo-gpu: governor loaded, mode=shared\n");
	return 0;
}

static void __exit sorabbyngo_gpu_exit(void)
{
	kobject_put(gpu_kobj);
}

module_init(sorabbyngo_gpu_init);
module_exit(sorabbyngo_gpu_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sorabbyngo GPU Governor");
