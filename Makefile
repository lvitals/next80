# next80 toolchain — top-level Makefile
#
# Targets:
#   all         Build n80, lk80 and lb80 (default)
#   install     Install binaries to $(DESTDIR)$(PREFIX)/bin
#   uninstall   Remove installed binaries
#   test        Run all test suites
#   clean       Remove build artifacts

# POSIX installation paths
PREFIX  ?= /usr/local
BINDIR  := $(DESTDIR)$(PREFIX)/bin

TOOLS   := n80 lk80 lb80

.PHONY: all install uninstall test clean $(TOOLS)

all: $(TOOLS)

n80:
	$(MAKE) -C n80

lk80:
	$(MAKE) -C lk80

lb80:
	$(MAKE) -C lb80

install: all
	install -d $(BINDIR)
	install -m 755 n80/n80   $(BINDIR)/n80
	install -m 755 lk80/lk80 $(BINDIR)/lk80
	install -m 755 lb80/lb80 $(BINDIR)/lb80

uninstall:
	rm -f $(BINDIR)/n80
	rm -f $(BINDIR)/lk80
	rm -f $(BINDIR)/lb80

test: all
	sh tests/run_tests.sh

clean:
	$(MAKE) -C n80  clean
	$(MAKE) -C lk80 clean
	$(MAKE) -C lb80 clean
