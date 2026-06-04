################################################################################
# sorabbyngo-linux — patched kernel for Sorabbyngo OS
# Fetches Linux 6.19.10, applies sorabbyngo-patches series, builds with
# sorabbyngo_x86_64_defconfig
################################################################################

SORABBYNGO_LINUX_VERSION       = 6.19.10
SORABBYNGO_LINUX_SITE          = https://cdn.kernel.org/pub/linux/kernel/v6.x
SORABBYNGO_LINUX_SOURCE        = linux-$(SORABBYNGO_LINUX_VERSION).tar.xz
SORABBYNGO_LINUX_SITE_METHOD   = wget
SORABBYNGO_LINUX_LICENSE       = GPL-2.0
SORABBYNGO_LINUX_LICENSE_FILES = COPYING

# Path to sorabbyngo-patches repo (checked out alongside sorabbyngo-os)
SORABBYNGO_PATCHES_DIR ?= $(BR2_EXTERNAL_SORABBYNGO_PATH)/../sorabbyngo-patches

define SORABBYNGO_LINUX_APPLY_PATCHES
	@echo "==> Applying Sorabbyngo security patches..."
	cd $(@D) && \
	for p in $(SORABBYNGO_PATCHES_DIR)/security/*.patch; do \
		echo "  applying: $$(basename $$p)"; \
		git apply --whitespace=nowarn "$$p" 2>/dev/null || \
		patch -p1 -s < "$$p" || \
		echo "  WARN: $$p did not apply cleanly — skipping"; \
	done
	@echo "==> Copying Sorabbyngo drivers..."
	cp -r $(SORABBYNGO_PATCHES_DIR)/drivers/sorabbyngo \
		$(@D)/drivers/sorabbyngo
	@echo "==> All patches applied."
endef

define SORABBYNGO_LINUX_CONFIGURE_CMDS
	$(SORABBYNGO_LINUX_APPLY_PATCHES)
	cp $(BR2_EXTERNAL_SORABBYNGO_PATH)/configs/sorabbyngo_x86_64_defconfig \
		$(@D)/.config
	$(MAKE) -C $(@D) ARCH=x86_64 \
		CROSS_COMPILE=$(TARGET_CROSS) olddefconfig
endef

define SORABBYNGO_LINUX_BUILD_CMDS
	$(MAKE) -C $(@D) ARCH=x86_64 \
		CROSS_COMPILE=$(TARGET_CROSS) \
		bzImage modules -j$(PARALLEL_JOBS)
endef

define SORABBYNGO_LINUX_INSTALL_TARGET_CMDS
	# Install kernel image
	$(INSTALL) -D -m 0644 $(@D)/arch/x86/boot/bzImage \
		$(BINARIES_DIR)/bzImage
	# Install modules
	$(MAKE) -C $(@D) ARCH=x86_64 \
		CROSS_COMPILE=$(TARGET_CROSS) \
		INSTALL_MOD_PATH=$(TARGET_DIR) \
		modules_install
	# Install NodeID + Byngonet modules explicitly
	find $(@D)/drivers/sorabbyngo -name "*.ko" | while read ko; do \
		$(INSTALL) -D -m 0644 "$$ko" \
			$(TARGET_DIR)/lib/modules/$(SORABBYNGO_LINUX_VERSION)/extra/$$(basename $$ko); \
	done
endef

$(eval $(generic-package))
