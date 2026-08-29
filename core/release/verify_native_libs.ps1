# Fail the deploy if any native library in the tree cannot actually load.
#
# Every previous check here asked whether a FILE existed. That is not the same
# question, and the difference cost a long investigation: libobjk_opencv.dll was
# present, correctly built for ARM64, correctly linked -- and could not load,
# because opencv_core4.dll imports z.dll and opencv_imgcodecs4.dll imports
# jpeg62/libpng16/tiff/libwebp*, none of which were ever shipped. Windows reports
# that as error 126, "The specified module could not be found", naming the
# library you asked for rather than the dependency that is actually absent.
#
# So this asks the real question, the same way the VM does: LoadLibraryW. If it
# returns null the deploy is broken, and a broken deploy must not be publishable.
#
# Usage:  powershell -File verify_native_libs.ps1 -DeployDir deploy-arm64
param(
  [Parameter(Mandatory = $true)][string]$DeployDir
)

$ErrorActionPreference = 'Stop'

$nativeDir = Join-Path $DeployDir 'lib\native'
$binDir    = Join-Path $DeployDir 'bin'

if (-not (Test-Path $nativeDir)) {
  Write-Output "No native library directory at $nativeDir - nothing to verify."
  exit 0
}

# The VM loads these with a plain LoadLibrary from a process whose executable
# lives in bin, so bin is on the default search path at runtime. Reproduce that
# here, or this check fails on libraries that would work perfectly well.
$env:PATH = "$((Resolve-Path $binDir).Path);$env:PATH"

Add-Type -Namespace Native -Name Loader -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr LoadLibraryW(string path);
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern bool FreeLibrary(System.IntPtr handle);
'@

$failed = @()
$loaded = 0

foreach ($dll in Get-ChildItem -Path $nativeDir -Filter *.dll -File) {
  $handle = [Native.Loader]::LoadLibraryW($dll.FullName)
  if ($handle -eq [System.IntPtr]::Zero) {
    $code = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    $text = (New-Object System.ComponentModel.Win32Exception($code)).Message
    $failed += [PSCustomObject]@{ Name = $dll.Name; Code = $code; Text = $text }
    Write-Output ("  FAIL  {0}  (error {1}: {2})" -f $dll.Name, $code, $text)
  }
  else {
    [void][Native.Loader]::FreeLibrary($handle)
    $loaded++
    Write-Output ("  ok    {0}" -f $dll.Name)
  }
}

Write-Output ''
if ($failed.Count -gt 0) {
  Write-Output '============================================================'
  Write-Output (" ERROR: {0} native librar{1} cannot load" -f $failed.Count, $(if ($failed.Count -eq 1) { 'y' } else { 'ies' }))
  Write-Output '============================================================'
  foreach ($f in $failed) {
    Write-Output ("   {0} - error {1}: {2}" -f $f.Name, $f.Code, $f.Text)
  }
  Write-Output ''
  Write-Output 'Error 126 means the library is present but something it imports is not.'
  Write-Output 'Find the missing import with:'
  Write-Output ("   dumpbin /dependents {0}\<name>.dll" -f $nativeDir)
  Write-Output 'and check each named DLL against the contents of bin.'
  exit 1
}

Write-Output ("All {0} native libraries load." -f $loaded)
exit 0
