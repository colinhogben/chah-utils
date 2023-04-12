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
	git-cane \
	git-ff \
	git-shove \
	git-smu \

WORK_UTILS = \
	defs undefs:defs \
	exp \
	srcdirs \
	git-cane \
	git-ff \
	git-shove \
	git-smu \

install:
	./install-files $(UTILS)

install-work:
	./install-files $(WORK_UTILS)
