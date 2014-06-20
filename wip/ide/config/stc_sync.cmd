set CONFIG_DIR=C:\Users\Randy\Documents\Code\objeck-lang\wip\ide\config
set WXWIDGETS_ROOT=D:\Code\wxWidgets-3.0.0

IF [%1] EQU [] goto COMMIT
IF %1 NEQ goto COMMIT
	copy wx_vc10_wxscintilla.vcxproj %WXWIDGETS_ROOT%\build\msw
	copy wx_vc10_wxscintilla.vcxproj.filters %WXWIDGETS_ROOT%\build\msw
	copy SciLexer.h %WXWIDGETS_ROOT%\src\stc\scintilla\include
	copy Catalogue.cxx SciLexer.h %WXWIDGETS_ROOT%\src\stc\scintilla\src\
	copy LexObjeck.cxx %WXWIDGETS_ROOT%\src\stc\scintilla\lexers
	goto END
	
:COMMIT	
	copy %WXWIDGETS_ROOT%\build\msw\wx_vc10_wxscintilla.vcxproj %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\build\msw\wx_vc10_wxscintilla.vcxproj.filters %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\src\stc\scintilla\include\SciLexer.h %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\src\stc\scintilla\src\Catalogue.cxx %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\src\stc\scintilla\lexers\LexObjeck.cxx %CONFIG_DIR%

:END