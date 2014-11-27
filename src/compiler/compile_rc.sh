#!/bin/sh  
for i in rc/*.obs  
	do  
	./obc -src "$i" -lib encrypt.obl,collect.obl,regex.obl,xml.obl,json.obl -dest a.obe 
done  
