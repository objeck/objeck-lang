# Fail deployment if a native library cannot load, or if a cross-compiled
# library has a non-system import that is absent from the deployed tree.
param(
  [Parameter(Mandatory = $true)][string]$DeployDir,
  [ValidateSet('x64', 'arm64')][string]$TargetArchitecture = 'x64'
)

$ErrorActionPreference = 'Stop'

$nativeDir = (Resolve-Path (Join-Path $DeployDir 'lib\native')).Path
$binDir = (Resolve-Path (Join-Path $DeployDir 'bin')).Path
$env:PATH = "$binDir;$env:PATH"

function Write-FailureSummary([object[]]$Failures) {
  Write-Output ''
  Write-Output '============================================================'
  Write-Output (" ERROR: {0} native-library verification failure(s)" -f $Failures.Count)
  Write-Output '============================================================'
  foreach ($failure in $Failures) {
    Write-Output ("   {0}" -f $failure)
  }
  exit 1
}

$loaded = 0
if ($TargetArchitecture -eq 'x64') {
  Add-Type -Namespace Native -Name Loader -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr LoadLibraryW(string path);
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern bool FreeLibrary(System.IntPtr handle);
'@

  $failed = @()
  foreach ($dll in Get-ChildItem -Path $nativeDir -Filter *.dll -File) {
    $handle = [Native.Loader]::LoadLibraryW($dll.FullName)
    if ($handle -eq [System.IntPtr]::Zero) {
      $code = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
      $text = (New-Object System.ComponentModel.Win32Exception($code)).Message
      $failed += ("{0} - error {1}: {2}" -f $dll.Name, $code, $text)
      Write-Output ("  FAIL  {0}  (error {1}: {2})" -f $dll.Name, $code, $text)
    }
    else {
      [void][Native.Loader]::FreeLibrary($handle)
      $loaded++
      Write-Output ("  ok    {0}" -f $dll.Name)
    }
  }

  if ($failed.Count -gt 0) {
    Write-FailureSummary $failed
  }
  Write-Output ("All {0} x64 native libraries load." -f $loaded)
}

# LoadLibrary cannot load ARM64 images in the x64 CI process (error 193), and an
# x64 LoadLibrary can be accidentally satisfied by runtimes installed on the
# build host. In both cases dumpbin supplies the self-contained-tree check. Walk
# imports from every VM-loadable native library.
# A dependency is satisfied only by this deploy or by Windows itself. VC runtime
# DLLs are intentionally required locally even when an x64 copy happens to be
# installed in System32 on the build host.
$dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
$localDlls = @{}
foreach ($dir in @($nativeDir, $binDir)) {
  foreach ($dll in Get-ChildItem -Path $dir -Filter *.dll -File) {
    $localDlls[$dll.Name] = $dll.FullName
  }
}

$requiredLocalPattern = '^(msvcp|vcruntime|concrt)[0-9_]*\.dll$'
$visited = @{}
$queue = [System.Collections.Generic.Queue[string]]::new()
foreach ($dll in Get-ChildItem -Path $nativeDir -Filter *.dll -File) {
  $queue.Enqueue($dll.FullName)
}

$failed = @()
while ($queue.Count -gt 0) {
  $path = $queue.Dequeue()
  $name = [System.IO.Path]::GetFileName($path)
  if ($visited.ContainsKey($name)) {
    continue
  }
  $visited[$name] = $true

  $output = & $dumpbin /nologo /dependents $path 2>&1
  if ($LASTEXITCODE -ne 0) {
    $failed += ("dumpbin could not inspect {0}: {1}" -f $name, ($output -join ' '))
    continue
  }

  foreach ($line in $output) {
    if ($line -notmatch '^\s+([A-Za-z0-9_.+-]+\.dll)\s*$') {
      continue
    }
    $dependency = $Matches[1]
    if ($dependency -match '^(api-ms-win-|ext-ms-win-)') {
      continue
    }
    if ($localDlls.ContainsKey($dependency)) {
      $queue.Enqueue($localDlls[$dependency])
      continue
    }

    $mustDeploy = $dependency -match $requiredLocalPattern
    $providedByWindows = Test-Path (Join-Path $env:SystemRoot "System32\$dependency")
    if ($mustDeploy -or -not $providedByWindows) {
      $failed += ("{0} imports missing {1}" -f $name, $dependency)
    }
  }
}

if ($failed.Count -gt 0) {
  Write-FailureSummary $failed
}

Write-Output ("{0} dependency closure verified across {1} libraries." -f $TargetArchitecture.ToUpper(), $visited.Count)
exit 0
