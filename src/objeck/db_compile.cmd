@echo off
cd deploy\bin
obc -src 'C:\Users\objec\Documents\Code\objeck-lang\src\compiler\lib_src\db.obs' -src 'C:\Users\objec\Documents\Code\objeck-lang\src\compiler\test_src\db_test.obs' -lib collect.obl -dest ..\..\db_test.obe
cd ..\..