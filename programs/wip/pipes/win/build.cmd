@echo off
cls
del /y *.exe
cl win_client.cpp /EHsc
cl win_server.cpp /EHsc