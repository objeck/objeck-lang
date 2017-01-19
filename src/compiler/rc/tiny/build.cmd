@echo off
del /q *.obe
obc -src scanner.obs,parser.obs,compiler.obs,emitter.obs -lib collect.obl -dest compiler.obe