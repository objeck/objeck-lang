$version = "3.3.5.2"

$version_number = $version.Replace(".", "")
$version_windows = $version.Replace(".", ",")
(Get-Content ..\..\debian\build.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | Set-Content ..\..\debian\build.sh
(Get-Content ..\shared\version.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_NUMBER@", $version_number } | Set-Content ..\shared\version.h
(Get-Content ..\compiler\compiler\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\compiler\compiler\objeck.rc
(Get-Content ..\vm\vm\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\vm\vm\objeck.rc
(Get-Content ..\utilities\utilities\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\utilities\utilities\objeck.rc
(Get-Content ..\vm\debugger\debugger\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\vm\debugger\debugger\objeck.rc
