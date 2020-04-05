obc -src ..\..\core\compiler\lib_src\query.obs -tar lib -lib csv,xml,misc,regex,gen_collect -dest ..\..\core\release\deploy64\lib\query.obl
obc -src test2.obs query.obs -lib query,csv,xml,misc,regex,gen_collect -dest test2.obe
obr test2.obe "D:\Temp\cs_metrics\data_input.csv"