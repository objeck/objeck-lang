@echo off
del /q *.obe
obc -src scanner.obs,parser.obs -lib collect.obl -dest intpr.obe