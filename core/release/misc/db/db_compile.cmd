@echo off
cd deploy\bin
obc -src ..\..\..\compiler\lib_src\db.obs -lib collect.obl -tar lib -dest ..\..\..\compiler\db.obl
copy ..\..\..\compiler\db.obl .
obc -src ..\..\..\compiler\test_src\debug.obs -lib db.obl,collect.obl -dest ..\..\db_test.obe
cd ..\..