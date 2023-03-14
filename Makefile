UTILS = \
	defs undefs:defs \
	pargs penv:pargs \
	lc \
	exp \
	cmddiff \
	cpifdiff \
	prod \
	prodls \
	srcdirs \

WORK_UTILS = \
	defs undefs:defs \
	exp \
	srcdirs \

install:
	./install-files $(UTILS)

install-work:
	./install-files $(WORK_UTILS)
