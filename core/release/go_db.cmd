@echo off
cd deploy\bin
obc -src ..\..\..\compiler\lib_src\query.obs -lib regex.obl,collect.obl -tar lib -dest ..\lib\query.obl
obc -src ..\..\..\..\programs\db\0_test.obs -lib regex.obl,query.obl,collect.obl -dest ..\..\test_query.obe
echo.
echo.
obr ..\..\test_query.obe
cd ..\..