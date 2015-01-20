# version string
$version = "3.3.5.2"

# alternative strings
$version_number = $version.Replace(".", "")
$version_posix = $version -replace "\.\d+$", ("-" + ($version.SubString($version.LastIndexOf(".") + 1)))
$version_posix_long = $version_posix + "-1"
$version_windows = $version.Replace(".", ",")

# update source version header
(Get-Content ..\shared\version.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_NUMBER@", $version_number } | Set-Content ..\shared\version.h

# update debain build files
(Get-Content ..\..\debian\build.in) | ForEach-Object { $_ -replace "@VERSION@", $version_posix } | Set-Content ..\..\debian\build.sh
(Get-Content ..\..\debian\files\changelog.in) | ForEach-Object { $_ -replace "@VERSION_LONG@", $version_posix_long } | Set-Content ..\..\debian\files\changelog

# update window resource files
(Get-Content ..\compiler\compiler\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\compiler\compiler\objeck.rc
(Get-Content ..\vm\vm\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\vm\vm\objeck.rc
(Get-Content ..\vm\debugger\debugger\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\vm\debugger\debugger\objeck.rc
(Get-Content ..\utilities\utilities\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\utilities\utilities\objeck.rc