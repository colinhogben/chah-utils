# Extract letter only, e.g. "gen-pub-51" -> "genpub"
letters = $(shell echo "$(1)" | sed -e s/[^A-Z]//ig)

# Get OS (or kernel) name and generic hostname
OS = $(call letters,$(shell (uname -o 2>/dev/null || uname -s)))
GENHOST = $(call letters,$(shell uname -n))

UTILS_ALL = \
	cconv \
	defs undefs:defs \
	exp \
	git-cane \
	git-ff \
	git-shove \
	git-smu \
	srcdirs \
	withvenv \

UTILS_SunOS = \

UTILS_GNULinux = \
	cmddiff \
	cpifdiff \
	dockclean \
	dockrun \
	eshed \
	lc \
	mvall cpall:mvall \
	pargs penv:pargs \
	prod \
	prodls \

UTILS_freia = \
	mountjac \
	mountsol:mountjac \
	mountmeta:mountjac \
	mountmast:mountjac \
	mountpcs:mountjac \

install:	cconv
	./install-files $(UTILS_ALL) $(UTILS_$(OS)) $(UTILS_$(GENHOST))

install-%:	cconv
	./install-files $(UTILS_ALL) $(UTILS_$*)

cconv:	cconv.c
	$(CC) -g -Wall -o $@ $<
