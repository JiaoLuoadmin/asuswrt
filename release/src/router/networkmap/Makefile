include ../.config

EXEC = networkmap
OBJS = function.o asusdiscovery.o networkmap.o

CFLAGS  += -Wall -DASUS -DBCMNVRAM -I. -I$(TOP)/shared -I$(TOP)/kernel_header/include -I$(SRCBASE)/include
CFLAGS  += -s -O2 -DNO_PARALLEL
LDFLAGS += -L$(TOP)/shared -lshared -L$(TOP)/nvram${BCMEX} -lnvram 
ifeq ($(RTCONFIG_BCMARM),y)
CFLAGS += -I$(SRCBASE)/common/include
LDFLAGS += -lgcc_s
endif

ifeq ($(RTCONFIG_QTN),y)
LDFLAGS += -L$(TOP)/libqcsapi_client -lqcsapi_client
endif

include $(SRCBASE)/.config

ifneq ($(findstring DSL,$(BUILD_NAME)),)
MODEL = $(subst -,_,$(BUILD_NAME))
else
MODEL = $(subst -,,$(BUILD_NAME))
endif

export CFLAGS += -D$(MODEL)

CFLAGS	+= -DDEBUG -DDEBUG_MORE
CFLAGS  += -DNMP_DB

all: $(EXEC) arpstorm

$(EXEC): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) $(CFLAGS)

arpstorm: shutils.o arpstorm.o
	$(CC) -o $@ $^ $(LDFLAGS) $(CFLAGS)

install:
	install -D $(EXEC) $(INSTALLDIR)/usr/sbin/$(EXEC)
	$(STRIP) $(INSTALLDIR)/usr/sbin/$(EXEC)
	install -D arpstorm $(INSTALLDIR)/usr/sbin/arpstorm
	$(STRIP) $(INSTALLDIR)/usr/sbin/arpstorm
clean: 
	rm -rf *.o $(EXEC) *~
