# ARM64 OpenCV Build Handoff

## What's happening

Fixing the Windows ARM64 build so all DLLs are present in the repo and
the release package is complete. Most of the work is done — this is the
one remaining piece.

vcpkg installed opencv4 as **split module DLLs** (not a combined world DLL)
into `C:\Users\objec\vcpkg\installed\arm64-windows\`. The repo currently
expects `opencv_world4120.lib/.dll` for ARM64, which doesn't exist in
vcpkg's split build. We need to switch the ARM64 configs to use the split
libs and commit the files.

## What's already done (pushed to master)

- `core/lib/openssl/win/include/nghttp2/` — nghttp2 headers committed; no vcpkg needed
- `core/lib/openssl/win/arm64/nghttp2.{lib,dll}` — nghttp2 ARM64 link + runtime
- `core/vm/vs/vm.vcxproj` — removed `$(LIB)` from LibraryPath (fixes 294 linker errors), removed vcpkg include deps
- `core/release/deploy_windows.cmd` — errorlevel guard after every devenv call; conditional opencv/onnx DLL copies
- `.github/workflows/release-build.yml` — removed Windows vcpkg install step (libs self-contained); ARM64 opencv CI step added

## What still needs to be done (on the ARM64 machine)

### Step 1 — Copy vcpkg opencv files into the repo

The split DLLs and libs are already installed on this machine from an earlier
`vcpkg install opencv4:arm64-windows` run. Copy them into the repo:

```cmd
cd C:\Users\objec\Documents\Code\objeck-lang
xcopy /y C:\Users\objec\vcpkg\installed\arm64-windows\bin\opencv_*4.dll core\lib\opencv\win\arm64\bin\
xcopy /y C:\Users\objec\vcpkg\installed\arm64-windows\lib\opencv_*4.lib core\lib\opencv\win\arm64\lib\
```

### Step 2 — Update `core\lib\opencv\vs\vs.vcxproj`

**Release|ARM64** (line 167) — change `AdditionalDependencies` from:
```
opencv_world4120.lib
```
to:
```
opencv_core4.lib;opencv_imgproc4.lib;opencv_imgcodecs4.lib;opencv_highgui4.lib;opencv_calib3d4.lib;opencv_dnn4.lib;opencv_features2d4.lib;opencv_flann4.lib;opencv_ml4.lib;opencv_objdetect4.lib;opencv_photo4.lib;opencv_stitching4.lib;opencv_video4.lib;opencv_videoio4.lib
```

**Debug|ARM64** (line 132) — change `AdditionalDependencies` from:
```
opencv_world4120d.lib
```
to:
```
opencv_core4d.lib;opencv_imgproc4d.lib;opencv_imgcodecs4d.lib;opencv_highgui4d.lib;opencv_calib3d4d.lib;opencv_dnn4d.lib;opencv_features2d4d.lib;opencv_flann4d.lib;opencv_ml4d.lib;opencv_objdetect4d.lib;opencv_photo4d.lib;opencv_stitching4d.lib;opencv_video4d.lib;opencv_videoio4d.lib
```

### Step 3 — Update `core\release\deploy_windows.cmd`

Find this ARM64 opencv block:
```cmd
if exist win\arm64\bin\opencv_world4120.dll (
    copy /y win\arm64\bin\opencv_world4120.dll ..\..\release\%TARGET%\bin
) else (
    echo Warning: win\arm64\bin\opencv_world4120.dll not found - OpenCV runtime unavailable
    echo   Install via: vcpkg install opencv4:arm64-windows
)
```

Replace it with:
```cmd
for %%f in (win\arm64\bin\opencv_*4.dll) do (
    copy /y %%f ..\..\release\%TARGET%\bin
)
```

### Step 4 — Update `.github\workflows\release-build.yml`

In the "Install OpenCV ARM64 runtime DLL via vcpkg" step, find:
```powershell
Get-ChildItem "$vcpkgBin\opencv_world*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
```

Change to:
```powershell
Get-ChildItem "$vcpkgBin\opencv_*4.dll" -ErrorAction SilentlyContinue | ForEach-Object {
```

### Step 5 — Build and verify

```cmd
cd C:\Users\objec\Documents\Code\objeck-lang\core\release
deploy_windows.cmd arm64
```

Verify:
- `deploy-arm64\bin\` contains `opencv_core4.dll`, `opencv_imgproc4.dll`, etc.
- `deploy-arm64\lib\native\libobjk_opencv.dll` exists

### Step 6 — Commit and push

```cmd
cd C:\Users\objec\Documents\Code\objeck-lang
git add core\lib\opencv\win\arm64\bin\opencv_*4.dll
git add core\lib\opencv\win\arm64\lib\opencv_*4.lib
git add core\lib\opencv\vs\vs.vcxproj
git add core\release\deploy_windows.cmd
git add .github\workflows\release-build.yml
git commit -m "fix: switch ARM64 opencv to vcpkg split module DLLs"
git push
git rm HANDOFF.md
git commit -m "remove handoff file"
git push
```
