# install location
PREFIX=$(DESTDIR)/$(or $(prefix),usr/local)

# where to find the KATCP library (change this in case katcp isn't included locally)
KATCP ?= katcp

# compiler flags, -Wall is just the right thing to do
CFLAGS += -Wall
CFLAGS += -O2

# where the C implementation has diverged from the katcp standard,
# force standard behaviour. Currently only one case: log-level affects
# all connections immediately in standard, otherwise local connection only
CFLAGS += -DKATCP_STRICT_CONFORMANCE

# how many messages to hold before forcing a flush, only useful
# on memory constrained systems. When this value is unset the
# system buffers as much as needed
#CFLAGS += -DKATCP_FLUSH_THRESHOLD=4

# enable floating point support (floating point sensor type)
# unless you don't want to use your fpu (or don't have one) enable this
CFLAGS += -DKATCP_USE_FLOATS

# enable support for KATCP_SENSOR_BIGINT (aka long long type)
CFLAGS += -DKATCP_ENABLE_LLINT

# perform redundant checks, abort if things don't look alright
# enabling this reduces performance and makes the system less
# resistant to failures, but will also notice failures sooner
CFLAGS += -DKATCP_CONSISTENCY_CHECKS

# count the number of allocations - parse only thusfar
CFLAGS += -DKATCP_MEMSTATS

# log selected noncritical failures to stderr
CFLAGS += -DKATCP_STDERR_ERRORS

# implement the katcp -T option
#CFLAGS += -DKATCP_ENABLE_TAGS

# log client messages
CFLAGS += -DKATCP_LOG_REQUESTS

# require unique client names - otherwise collisions are permitted
# probably needed by new timer sensors, as they are looked up by name
CFLAGS += -DKATCP_UNIQUE_CLIENT_NAMES

# enable manage old katcp subprocesses interface (job)
CFLAGS += -DKATCP_SUBPROCESS

# enable newer code: contains the new flat/duplex logic to replace older interface
CFLAGS += -DKATCP_EXPERIMENTAL
# enable even more experimental code: switch to new duplex logic by default: will break users of job interface and nonsense
#CFLAGS += -DKATCP_EXPERIMENTAL=2

# keep older code
CFLAGS += -DKATCP_DEPRECATED

# enable heap timers - for old code
CFLAGS += -DKATCP_HEAP_TIMERS

# respond to trap TERM signal in main loop
CFLAGS += -DKATCP_TRAP_TERM

# inhibit sensor-{value,list} prefix matching, unless it is on a . boundary
#CFLAGS += -DKATCP_STRICT_SENSOR_MATCH
# only match sensor-{value,list} if the names match exactly
#CFLAGS += -DKATCP_STRICTER_SENSOR_MATCH

# pick a protocol reversion from the list. Making up
# your own major values doesn't work unless you also implement what
# the protocol requires
CFLAGS += -DKATCP_PROTOCOL_MAJOR_VERSION=5 -DKATCP_PROTOCOL_MINOR_VERSION=0
#CFLAGS += -DKATCP_PROTOCOL_MAJOR_VERSION=4 -DKATCP_PROTOCOL_MINOR_VERSION=9

# amount of debugging logic to enable
#CFLAGS += -DDEBUG=4
#CFLAGS += -DDEBUG
CFLAGS += -ggdb

# what compiler to use
CC = $(CROSS_COMPILE)gcc
# C++ implements more finicky type checks... so this gives loads of warnings
#CC = g++ -fpermissive

GITVER = $(shell git describe --always --tags --long --dirty 2> /dev/null || echo unknown)

INSTALL = install
RM = rm -f
AR = ar
ECHO = echo
LN = ln -s
TAR = tar

.PHONY: all clean install
.SUFFIXES:
