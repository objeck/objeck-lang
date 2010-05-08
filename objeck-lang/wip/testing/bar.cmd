@echo off
echo ==== %1 ==== 
obc -src %1 -lib struct.obl -dest _p%2.obe 
obr _p%2.obe Hello
echo ----------------------