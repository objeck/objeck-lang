del code_gen.obe
obc -src sdl_emitter.obs,sdl_parser.obs,sdl_scanner.obs -lib collect.obl -dest code_gen.obe
IF NOT "%~1"=="" IF NOT "%~2"==""  (
	obr code_gen.obe %1 %2
)