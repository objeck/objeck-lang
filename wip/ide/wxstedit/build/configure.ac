# ======================================================================================
# Author: Francesco Montorsi
# RCS-ID: $Id: configure.ac,v 1.13 2007/03/06 23:58:00 jrl1 Exp $
#
# A basic "configure.ac" for a wxCode component.
# See
#    http://www.gnu.org/software/autoconf/manual/autoconf-2.57/html_mono/autoconf.html
# for more info about autoconf and the autoconf macros.
# ======================================================================================

# Init this configure script with basic info about the component
# (DON'T PUT ANYTHING BEFORE AC_INIT, JUST COMMENTS)
AC_INIT([wxstedit], [1.2.4], [jrl1@sourceforge.net])
AC_CONFIG_AUX_DIR(build)

# ENABLES/DISABLES THE DEBUG MODE FOR THIS CONFIGURE SCRIPT
#WX_DEBUG_CONFIGURE=1

# we need the wxCode/build/bakefiles in your local repository to generate a configure
# script from this configure.ac file since wxcode.m4 is required....
m4_include(../../../build/autoconf/wxcode.m4)
WXCODE_INIT



## CONFIGURE OPTIONS
##
## Before starting checks, declare the options of this configure script
## Here you should use the AC_ARG_ENABLE and AC_ARG_WITH macros
## to add to this configure script the --enable-XXX and/or --with-XXX
## options required. If you did not use any <option> tag in your
## component's bakefile, then you can leave this section as is
#####################################################################

WXCODE_OPTIONS([debug,unicode,shared,toolkit,wxshared,wxversion])



## CONFIGURE CHECKS
##
## Here you should use the AC_CHECK_LIB, AC_COMPILE_IFELSE,
## AC_LINK_IFELSE, etc macros to check that the libraries required
## by your component exist on the host machine and match your
## required options (version, build settings, etc)
#####################################################################

WXCODE_CHECKS([2.6.0], [stc,html,adv,core,base])      # we need at least wxWidgets 2.6 to work


# end in a nice way the configure script
WXCODE_END
