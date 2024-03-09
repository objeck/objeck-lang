set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang
obc -src %OBJECK_ROOT%\core\compiler\lib_src\net_common.obs,%OBJECK_ROOT%\core\compiler\lib_src\net.obs,%OBJECK_ROOT%\core\compiler\lib_src\net_secure.obs -lib json -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\net.obl

obc -src %OBJECK_ROOT%\programs\deploy\rss_https_xml_15 -lib encrypt,net,odbc,rss,regex,misc,xml,json

if [%1] NEQ [brun] goto end
    obr %OBJECK_ROOT%\programs\deploy\rss_https_xml_15 https://www.techmeme.com/feed.xml out.txt
:end