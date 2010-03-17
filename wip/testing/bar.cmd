@echo off
echo ==== %1 ==== 
obc -src %1 -lib struct.obl -dest a.obe
obr a.obe
echo ----------------------