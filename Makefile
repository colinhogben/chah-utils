UTILS = \
	defs undefs:defs \
	pargs penv:pargs \
	lc \
	exp \
	cmddiff \
	cpifdiff \
	prod \
	prodls \

WORK_UTILS = \
	defs undefs:defs \
	exp \

install:
	./install-files $(UTILS)

install-work:
	./install-files $(WORK_UTILS)
