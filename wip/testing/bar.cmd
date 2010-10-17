@echo off
echo ==== %1 ==== 
obc -src %1 -lib struct.obl -dest _p%1.obe 
obr _p%1.obe Hello
echo ----------------------