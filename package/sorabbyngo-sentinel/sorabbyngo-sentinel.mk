################################################################################
# sorabbyngo-sentinel
################################################################################
SORABBYNGO_SENTINEL_VERSION     = 1.0.0
SORABBYNGO_SENTINEL_SITE        = $(BR2_EXTERNAL_SORABBYNGO_PATH)/../sentinel
SORABBYNGO_SENTINEL_SITE_METHOD = local

define SORABBYNGO_SENTINEL_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/opt/sorabbyngo/sentinel
	$(INSTALL) -D -m 0755 $(@D)/sentinel_daemon.py \
		$(TARGET_DIR)/opt/sorabbyngo/sentinel/sentinel_daemon.py
	$(INSTALL) -D -m 0755 $(@D)/sentinel_watcher.py \
		$(TARGET_DIR)/opt/sorabbyngo/sentinel/sentinel_watcher.py
endef

$(eval $(generic-package))
