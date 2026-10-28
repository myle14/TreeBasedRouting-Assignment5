CONTIKI_PROJECT = group4
TARGET=sky
all: $(CONTIKI_PROJECT)

# Vo hieu hoa uIP de he thong build nap Rime stack
# CFLAGS = -DWITH_UIP=1

# Chi thi cho he thong build nap toan bo Rime stack va driver lien quan
CONTIKI_WITH_RIME = 1

# Tat co IPv6 trong he thong build
CONTIKI_WITH_IPV6 = 0

# Khai bao su dung file project-conf.h
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# DEFINES=NETSTACK_CONF_RDC=contikimac_driver,NETSTACK_CONF_MAC=csma_driver
# DEFINES=NETSTACK_CONF_RDC=xmac_driver,NETSTACK_CONF_MAC=csma_driver
# DEFINES=NETSTACK_CONF_RDC=nullrdc_driver,NETSTACK_CONF_MAC=csma_driver

CONTIKI = ../..
include $(CONTIKI)/Makefile.include
