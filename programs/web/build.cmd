obc -src ..\..\core\compiler\lib_src\fcgi_web.obs -lib fcgi,gen_collect,net,misc -tar lib -dest ..\..\core\lib\fcgi_web.obl
copy /y ..\..\core\lib\fcgi_web.obl ..\..\core\release\deploy64\lib\
obc -src %1.obs -lib fcgi,gen_collect,net -tar web -dest %1.obw