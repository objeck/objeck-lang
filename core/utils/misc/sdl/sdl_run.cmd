@echo off
cd deploy\bin
obc -src '..\..\..\compiler\test_src\sdl\sdl1.obs' -lib sdl.obl,collect.obl -dest ..\sdl1.obe
obr ..\sdl1.obe
cd ..\..