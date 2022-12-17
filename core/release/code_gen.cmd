cd deploy\bin
obc -src ..\..\..\..\programs\sdl\code_gen\sdl_emitter.obs,..\..\..\..\programs\sdl\code_gen\sdl_parser.obs,..\..\..\..\programs\sdl\code_gen\sdl_scanner.obs -lib collect.obl -dest ..\..\code_gen.obe
obr ..\..\code_gen.obe %1 %2
cd ..\..