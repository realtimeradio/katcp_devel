KATCP ?= ../katcp
RFDC ?= ../rfdc
RFSOC_PLATFORM ?=
SYSROOT ?= ../sysroot

include ../Makefile.inc

INC = -I$(KATCP)
INC += -I$(RFDC)
#LIB = -L$(KATCP) -lkatcp -ldl -lz -lmagic
LIB = -L$(SYSROOT)/lib -L$(SYSROOT)/usr/lib -L$(KATCP) -lkatcp -ldl -lz -lrfdc -lmetal
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
SRC = main.c raw.c loadbof.c tg.c tapper.c hwmon.c upload.c subprocess.c ev.c phy.c alpaca_i2c_utils.c alpaca_rfclks.c rfsoc.c

OBJ = $(patsubst %.c,%.o,$(SRC))
all: $(SERVER)

#$(SERVER): $(OBJ) 
$(SERVER): $(OBJ) $(KATCP)/libkatcp.a
	$(CC) -o $@ --sysroot=$(SYSROOT) $(OBJ) $(LIB)

clean: 
	$(RM) -f $(SERVER) *.o test-*

%.o: %.c
	$(CC) $(CFLAGS) --sysroot=$(SYSROOT) $(RFSOC_PLATFORM) -c $< -o $@ $(INC)

install: all
	$(INSTALL) $(SERVER) $(PREFIX)/sbin

test-bof: bof.c 
	$(CC) $(CFLAGS) --sysroot=$(SYSROOT) -DSTANDALONE -o $@ $^ -I../katcp

