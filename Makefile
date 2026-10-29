CONTIKI_PROJECT = group4
TARGET = sky
all: $(CONTIKI_PROJECT)

# Bat Rime stack và tat co IPv6 trong he thong build
CONTIKI_WITH_RIME = 1
CONTIKI_WITH_IPV6 = 0

# Chi đinh nap tep cau hinh tham so project-conf.h
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

CONTIKI = ../..
include $(CONTIKI)/Makefile.include


