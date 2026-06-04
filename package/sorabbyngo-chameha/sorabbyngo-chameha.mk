################################################################################
# sorabbyngo-chameha
################################################################################
SORABBYNGO_CHAMEHA_VERSION     = 1.0.0
SORABBYNGO_CHAMEHA_SITE        = $(BR2_EXTERNAL_SORABBYNGO_PATH)/../board/sorabbyngo/overlay/opt/sorabbyngo/chameha
SORABBYNGO_CHAMEHA_SITE_METHOD = local

define SORABBYNGO_CHAMEHA_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/opt/sorabbyngo/chameha/first-boot
	$(INSTALL) -D -m 0755 $(@D)/first-boot/chameha_firstboot.py \
		$(TARGET_DIR)/opt/sorabbyngo/chameha/first-boot/chameha_firstboot.py
endef

$(eval $(generic-package))
