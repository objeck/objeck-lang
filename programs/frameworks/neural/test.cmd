
if [%1] NEQ [data] goto end
obc data\gen
obr data\gen > data\test.csv
:end

obc -src ..\..\..\core\compiler\lib_src\ml.obs -tar lib -lib csv -dest ..\..\..\core\lib\ml.obl
copy /y ..\..\..\core\lib\ml.obl ..\..\..\core\release\deploy64\lib\ml.obl
obc -src nn6 -lib csv,ml
obr nn6 data\test.csv