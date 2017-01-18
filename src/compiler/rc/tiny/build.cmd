@echo off
del /q *.obe
obc -src scanner.obs,parser.obs,compiler.obs -lib collect.obl -dest compiler.obe