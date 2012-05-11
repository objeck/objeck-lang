mod_hello.la: mod_hello.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_hello.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_hello.la
