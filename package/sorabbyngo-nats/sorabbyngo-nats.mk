################################################################################
# sorabbyngo-nats
################################################################################
SORABBYNGO_NATS_VERSION = 2.10.18
SORABBYNGO_NATS_ARCH    = amd64
SORABBYNGO_NATS_SITE    = https://github.com/nats-io/nats-server/releases/download/v$(SORABBYNGO_NATS_VERSION)
SORABBYNGO_NATS_SOURCE  = nats-server-v$(SORABBYNGO_NATS_VERSION)-linux-$(SORABBYNGO_NATS_ARCH).tar.gz

define SORABBYNGO_NATS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/nats-server \
		$(TARGET_DIR)/usr/bin/nats-server
	mkdir -p $(TARGET_DIR)/etc/sorabbyngo/nats
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_SORABBYNGO_PATH)/nats/sorabbyngo.conf \
		$(TARGET_DIR)/etc/sorabbyngo/nats/sorabbyngo.conf
endef

$(eval $(generic-package))
