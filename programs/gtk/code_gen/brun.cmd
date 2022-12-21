@echo off

del code_gen.obe ..\..\..\core\lib\gtk3.obl

obc -src sdl_emitter.obs,sdl_parser.obs,sdl_scanner.obs -dest code_gen.obe
IF NOT "%~1"=="" IF NOT "%~2"=="" IF NOT "%~3"=="" (
	obr code_gen.obe %1 %2 %3
)

obc -src ..\..\..\core\compiler\lib_src\gtk3.obs -tar lib -dest ..\..\..\core\lib\gtk3.obl