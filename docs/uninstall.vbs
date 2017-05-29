' Written by Sidd

Option Explicit 'all variables must be defined

Dim oReg, oShell, oFSO 
Dim UninstallString, ProductCode
Dim strComputer, colItems, objWMIService, objItem
Dim strKeyPath, subkey, arrSubKeys
strComputer = "." 

'********************************
'Enter Product Code Of The Application Here That You Want To Uninstall within the Bracket 
ProductCode = "{A1931BE6-A793-4ECE-B303-66B69408440F}" 

'********************************

' Get scripting objects needed throughout script.
Set oShell = CreateObject("WScript.Shell")

'**************************
UninstallString = "MsiExec.exe /X" & ProductCode & " /qn" & " /norestart"

Const HKEY_LOCAL_MACHINE = &H80000002

Set oReg=GetObject("winmgmts:{impersonationLevel=impersonate}!\\" &_ 
strComputer & "\root\default:StdRegProv")
 
strKeyPath = "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"
oReg.EnumKey HKEY_LOCAL_MACHINE, strKeyPath, arrSubKeys
 
For Each subkey In arrSubKeys 
 
 IF subkey = ProductCode Then 
 oShell.Run UninstallString, 1, True
 End If

Next

Set oShell = Nothing
Set oReg = Nothing
'************* End Code ************