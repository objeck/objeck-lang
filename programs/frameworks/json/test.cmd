
obc -src ..\..\..\core\compiler\lib_src\json_stream.obs -tar lib -dest ..\..\..\core\release\deploy64\lib\json_stream.obl
obc -src test4 -lib json_stream

if [%1]==[] goto end
obr test4 %1
:end