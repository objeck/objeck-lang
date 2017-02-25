#!/bin/sh  
for i in $1/*.obs  
	do  
	./obc -src "$i" -lib odbc.obl,encrypt.obl,collect.obl,regex.obl,xml.obl,json.obl -dest a.obe 
done  
