################################################################################
# sorabbyngo-linux — patched kernel for Sorabbyngo OS
################################################################################
SORABBYNGO_LINUX_VERSION       = 6.19.10
SORABBYNGO_LINUX_SITE          = https://cdn.kernel.org/pub/linux/kernel/v6.x
SORABBYNGO_LINUX_SOURCE        = linux-$(SORABBYNGO_LINUX_VERSION).tar.xz
SORABBYNGO_LINUX_SITE_METHOD   = wget
SORABBYNGO_LINUX_LICENSE       = GPL-2.0
SORABBYNGO_LINUX_LICENSE_FILES = COPYING

SORABBYNGO_PATCHES_DIR = $(BR2_EXTERNAL_SORABBYNGO_PATH)/../sorabbyngo-patches

define SORABBYNGO_LINUX_CONFIGURE_CMDS
	@echo "==> Applying Sorabbyngo security patches..."
	cd $(@D) && \
	for p in $(SORABBYNGO_PATCHES_DIR)/security/*.patch; do \
		echo "  applying: $$(basename $$p)"; \
		patch -p1 --forward --reject-file=/dev/null -s < "$$p" || \
		echo "  WARN: $$p skipped"; \
	done
	@echo "==> Copying Sorabbyngo drivers..."
	mkdir -p $(@D)/drivers/sorabbyngo
	cp -r $(SORABBYNGO_PATCHES_DIR)/drivers/sorabbyngo/. \
		$(@D)/drivers/sorabbyngo/
	@echo "==> Applying kernel config..."
	cp $(SORABBYNGO_PATCHES_DIR)/sorabbyngo.config $(@D)/.config
	$(MAKE) -C $(@D) ARCH=x86_64 \
		CROSS_COMPILE=$(TARGET_CROSS) olddefconfig
	@echo "==> Verifying key configs..."
	grep "CONFIG_SERIAL_8250=y" $(@D)/.config || echo "WARN: SERIAL_8250 missing"
	grep "CONFIG_ATA_PIIX=y" $(@D)/.config || echo "WARN: ATA_PIIX missing"
endef

define SORABBYNGO_LINUX_BUILD_CMDS
	$(MAKE) -C $(@D) ARCH=x86_64 \
		CROSS_COMPILE=$(TARGET_CROSS) \
		bzImage modules -j$(PARALLEL_JOBS)
endef

define SORABBYNGO_LINUX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/arch/x86/boot/bzImage \
		$(BINARIES_DIR)/bzImage
	$(MAKE) -C $(@D) ARCH=x86_64 \
		CROSS_COMPILE=$(TARGET_CROSS) \
		INSTALL_MOD_PATH=$(TARGET_DIR) \
		modules_install
	find $(@D)/drivers/sorabbyngo -name "*.ko" 2>/dev/null | while read ko; do \
		$(INSTALL) -D -m 0644 "$$ko" \
			$(TARGET_DIR)/lib/modules/$(SORABBYNGO_LINUX_VERSION)/extra/$$(basename $$ko); \
	done
endef

$(eval $(generic-package))
