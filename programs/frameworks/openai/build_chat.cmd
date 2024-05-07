set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang

obc -src %OBJECK_ROOT%\core\compiler\lib_src\openai.obs -lib json,net,misc -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\openai.obl


obc -src openai_chat -lib openai,csv,net,json,misc
obr openai_chat %1 %2

