CONFIG_DIR=~/Documents/Code/objeck-lang/wip/ide/config
WXWIDGETS_ROOT=~/Documents/Code/wxWidgets-3.0.1

if [ ! -z "$1" ]  && [ "$1" = "deploy" ]; then
	cp Makefile $WXWIDGETS_ROOT
	cp Catalogue.cxx $WXWIDGETS_ROOT/src/stc/scintilla/src/
	cp LexObjeck.cxx $WXWIDGETS_ROOT/src/stc/scintilla/lexers
	cp edit.h $WXWIDGETS_ROOT/samples/stc
	cp edit.cpp $WXWIDGETS_ROOT/samples/stc
	cp prefs.h $WXWIDGETS_ROOT/samples/stc
	cp prefs.cpp $WXWIDGETS_ROOT/samples/stc
	cp stc.h $WXWIDGETS_ROOT/include/wx/stc
	cp SciLexer.h $WXWIDGETS_ROOT/src/stc/scintilla/include
else
	cp $WXWIDGETS_ROOT/Makefile $CONFIG_DIR
	cp $WXWIDGETS_ROOT/include/wx/stc/stc.h $CONFIG_DIR
	cp $WXWIDGETS_ROOT/src/stc/scintilla/src/Catalogue.cxx $CONFIG_DIR
	cp $WXWIDGETS_ROOT/src/stc/scintilla/lexers/LexObjeck.cxx $CONFIG_DIR	
	cp $WXWIDGETS_ROOT/samples/stc/edit.h $CONFIG_DIR
	cp $WXWIDGETS_ROOT/samples/stc/edit.cpp $CONFIG_DIR
	cp $WXWIDGETS_ROOT/samples/stc/prefs.h $CONFIG_DIR
	cp $WXWIDGETS_ROOT/samples/stc/prefs.cpp $CONFIG_DIR
	cp $WXWIDGETS_ROOT/src/stc/scintilla/include/SciLexer.h $CONFIG_DIR
fi
