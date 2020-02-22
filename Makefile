UTILS = defs undefs:defs pargs penv:pargs lc exp cmddiff
WORK_UTILS = #defs undefs

install:
	./install-files $(UTILS)

install-work:
	./install-files $(WORK_UTILS)
