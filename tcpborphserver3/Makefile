KATCP ?= ../katcp
RFDC ?= ../rfdc_v8.1
RFSOC_PLATFORM ?=
SYSROOT ?= ../sysroot
IS_RFSOC ?= 0

include ../Makefile.inc

INC = -I$(KATCP)
ifeq ($(IS_RFSOC), 1)
INC += -I$(RFDC)
endif

#LIB = -L$(KATCP) -lkatcp -ldl -lz -lmagic
LIB = -L$(SYSROOT)/lib -L$(SYSROOT)/usr/lib -L$(KATCP) -lkatcp -ldl -lz
ifeq ($(IS_RFSOC), 1)
LIB += -L$(RFDC) -lrfdc -lmetal
endif

#CFLAGS += -fPIC
CFLAGS += -ggdb
#CFLAGS += -DDEBUG=2
#CFLAGS += -DDEBUG
#CFLAGS += -DPROFILE
# crash rather than limp on 
#CFLAGS += -DFAILFAST
# use internal hardware monitor
#CFLAGS += -DINTERNAL_HWMON

SERVER = tcpborphserver3
SRC = main.c raw.c loadbof.c tg.c tapper.c hwmon.c upload.c subprocess.c ev.c phy.c
ifeq ($(IS_RFSOC), 1)
SRC += alpaca_i2c_utils.c alpaca_spi.c alpaca_rfclks.c rfsoc.c
endif

OBJ = $(patsubst %.c,%.o,$(SRC))
all: $(SERVER)

#$(SERVER): $(OBJ) 
$(SERVER): $(OBJ) $(KATCP)/libkatcp.a
	$(CC) -o $@ --sysroot=$(SYSROOT) $(OBJ) $(LIB)

clean: 
	$(RM) -f $(SERVER) *.o test-*

%.o: %.c
	$(CC) $(CFLAGS) --sysroot=$(SYSROOT) $(RFSOC_PLATFORM) -DIS_RFSOC=$(IS_RFSOC) -c $< -o $@ $(INC)

# Ensure that kcpfpg gets installed
install: all
	$(MAKE) -C ../fpg $@
	$(INSTALL) $(SERVER) $(PREFIX)/sbin

test-bof: bof.c 
	$(CC) $(CFLAGS) --sysroot=$(SYSROOT) -DSTANDALONE -o $@ $^ -I../katcp

