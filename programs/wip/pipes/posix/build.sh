#!/bin/sh

rm client server /tmp/objk
g++ posix_client.cpp -o client
g++ posix_server.cpp -o server
