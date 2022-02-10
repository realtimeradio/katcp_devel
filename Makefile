include Makefile.inc 

###############################################################################

LIBRARY = katcp
APPS = kcs cmd examples sq bulkread tmon log fmon tcpborphserver3 msg delay par sgw xport con dmon smon fpg run mpx gmon
ifeq ($(findstring KATCP_DEPRECATED,$(CFLAGS)),KATCP_DEPRECATED)
APPS += modules
endif

MISC = scripts misc 

SNAPSHOT=katcp-$(GITVER)

SELECTED = kcs cmd tmon log msg par con fpg run mpx 
EVERYTHING = $(LIBRARY) $(APPS)
TOPLEVEL = COPYING INSTALL Makefile Makefile.inc README TODO

###############################################################################

all: $(patsubst %,%-all,$(EVERYTHING))
clean: $(patsubst %,%-clean,$(EVERYTHING))
install: $(patsubst %,%-install,$(SELECTED))

$(patsubst %,%-all,$(APPS)): $(patsubst %,%-all,$(LIBRARY))

%-all %-clean %-install:
	$(MAKE) -C $(shell echo $@ | cut -f1 -d- ) KATCP=../$(KATCP) $(shell echo $@ | cut -f2 -d-)

dist: clean
	$(RM) $(SNAPSHOT) $(SNAPSHOT).tar.gz
	$(LN) . $(SNAPSHOT)
	$(TAR) czhlvf $(SNAPSHOT).tar.gz $(foreach f,$(TOPLEVEL) $(EVERYTHING),$(SNAPSHOT)/$(f))
	$(RM) $(SNAPSHOT)

###############################################################################
# old style build, can not be run in parallel 
#
# all: all-dir
# clean: clean-dir
# install: install-dir
#
# warning: below rewrites KATCP for subdirectory
# all-dir clean-dir install-dir: 
#	@for d in $(SUB); do if ! $(MAKE) -C $$d KATCP=../$(KATCP) $(subst -dir,,$@) ; then exit; fi; done
