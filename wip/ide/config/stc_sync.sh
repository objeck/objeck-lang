CONFIG_DIR=~/Documents/Code/objeck-lang/wip/ide/config
WXWIDGETS_ROOT=~/Documents/Code/wxWidgets-3.0.0

if [ ! -z "$1" ]  && [ "$1" = "deploy" ]; then
	cp Makefile $WXWIDGETS_ROOT/
	cp wx_vc10_wxscintilla.vcxproj.filters $WXWIDGETS_ROOT/build/msw
	cp SciLexer.h $WXWIDGETS_ROOT/src/stc/scintilla/include
	cp stc.h $WXWIDGETS_ROOT/include/wx/stc/
	cp Catalogue.cxx SciLexer.h $WXWIDGETS_ROOT/src/stc/scintilla/src/
	cp LexObjeck.cxx $WXWIDGETS_ROOT/src/stc/scintilla/lexers
else	
	cp $WXWIDGETS_ROOT/Makefile $CONFIG_DIR
	cp $WXWIDGETS_ROOT/build/msw/wx_vc10_wxscintilla.vcxproj.filters $CONFIG_DIR
	cp $WXWIDGETS_ROOT/src/stc/scintilla/include/SciLexer.h $CONFIG_DIR
	cp $WXWIDGETS_ROOT/include/wx/stc/stc.h $CONFIG_DIR
	cp $WXWIDGETS_ROOT/src/stc/scintilla/src/Catalogue.cxx $CONFIG_DIR
	cp $WXWIDGETS_ROOT/src/stc/scintilla/lexers/LexObjeck.cxx $CONFIG_DIR
fi
