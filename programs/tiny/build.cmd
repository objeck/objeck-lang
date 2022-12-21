@echo off
del /q *.obe
obc -src compiler\scanner.obs,compiler\parser.obs,compiler\compiler.obs,compiler\emitter.obs,common\instructions.obs -dest compiler.obe
obc -src vm\interpreter.obs,common\instructions.obs -dest vm.obe