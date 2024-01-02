# Extract letter only, e.g. "gen-pub-51" -> "genpub"
letters = $(shell echo "$(1)" | sed -e s/[^A-Z]//ig)

# Get OS (or kernel) name and generic hostname
OS = $(call letters,$(shell (uname -o 2>/dev/null || uname -s)))
GENHOST = $(call letters,$(shell unmame -n))

UTILS_ALL = \
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
	pargs penv:pargs \
	prod \
	prodls \

install:
	./install-files $(UTILS_ALL) $(UTILS_$(OS)) $(UTILS_$(GENHOST))

install-%:
	./install-files $(UTILS_ALL) $(UTILS_$*)

