@echo off
echo ==== %1 ==== 
obc -src %1 -lib struct.obl,xml.obl -opt s3 -dest _p%1.obe 
obr _p%1.obe Hello
echo ----------------------