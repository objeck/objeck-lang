set CONFIG_DIR="C:\Users\Randy\Documents\Code\objeck-lang\wip\ide\config"
set WXWIDGETS_ROOT="D:\Code\wxWidgets-3.0.1"

IF [%1] EQU [] goto COMMIT
IF %1 NEQ deploy goto COMMIT
	copy wx_vc10_wxscintilla.vcxproj %WXWIDGETS_ROOT%\build\msw
	copy wx_vc10_wxscintilla.vcxproj.filters %WXWIDGETS_ROOT%\build\msw
	copy wx_vc12_wxscintilla.vcxproj %WXWIDGETS_ROOT%\build\msw
	copy wx_vc12_wxscintilla.vcxproj.filters %WXWIDGETS_ROOT%\build\msw
	copy SciLexer.h %WXWIDGETS_ROOT%\src\stc\scintilla\include
	copy stc.h %WXWIDGETS_ROOT%\include\wx\stc\
	copy Catalogue.cxx %WXWIDGETS_ROOT%\src\stc\scintilla\src\
	copy LexObjeck.cxx %WXWIDGETS_ROOT%\src\stc\scintilla\lexers
	copy edit.h %WXWIDGETS_ROOT%\samples\stc\
	copy edit.cpp %WXWIDGETS_ROOT%\samples\stc\
	copy prefs.h %WXWIDGETS_ROOT%\samples\stc\
	copy prefs.cpp %WXWIDGETS_ROOT%\samples\stc\
	goto END
:COMMIT	
	copy %WXWIDGETS_ROOT%\build\msw\wx_vc10_wxscintilla.vcxproj %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\build\msw\wx_vc10_wxscintilla.vcxproj.filters %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\build\msw\wx_vc12_wxscintilla.vcxproj  %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\build\msw\wx_vc12_wxscintilla.vcxproj.filters  %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\src\stc\scintilla\include\SciLexer.h %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\include\wx\stc\stc.h %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\src\stc\scintilla\src\Catalogue.cxx %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\src\stc\scintilla\lexers\LexObjeck.cxx %CONFIG_DIR%	
	copy %WXWIDGETS_ROOT%\samples\stc\edit.h %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\samples\stc\edit.cpp %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\samples\stc\prefs.h %CONFIG_DIR%
	copy %WXWIDGETS_ROOT%\samples\stc\prefs.cpp %CONFIG_DIR%
:END