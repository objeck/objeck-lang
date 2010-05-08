@echo off
set %i=0
for %%x in (*.obs) do set /a %i += 1; call bar.cmd %%x %%i; 
	
	 