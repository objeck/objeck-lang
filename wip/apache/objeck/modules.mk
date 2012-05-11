mod_objeck.la: mod_objeck.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_objeck.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_objeck.la
