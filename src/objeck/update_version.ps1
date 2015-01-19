$version = "3.3.5.2"

$version_number = $version.Replace(".", "")
(Get-Content ..\..\debian\build.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | Set-Content ..\..\debian\build.sh
(Get-Content ..\shared\version.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_NUMBER@", $version_number } | Set-Content ..\shared\version.h
