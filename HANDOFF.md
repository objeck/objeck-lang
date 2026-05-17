# ARM64 OpenCV — One Step Remaining

All code changes are done and pushed. The only remaining task is to commit
the ARM64 opencv DLL files from this machine.

## What to do (run this on the ARM64 machine)

```cmd
cd C:\Users\objec\Documents\Code\objeck-lang
git pull
xcopy /y C:\Users\objec\vcpkg\installed\arm64-windows\bin\opencv_*4.dll core\lib\opencv\win\arm64\bin\
xcopy /y C:\Users\objec\vcpkg\installed\arm64-windows\lib\opencv_*4.lib core\lib\opencv\win\arm64\lib\
git add core\lib\opencv\win\arm64\bin\opencv_*4.dll
git add core\lib\opencv\win\arm64\lib\opencv_*4.lib
git commit -m "add ARM64 opencv split module DLLs and libs from vcpkg"
git push
git rm HANDOFF.md
git commit -m "remove handoff file"
git push
```

## What was already done

- `core/lib/opencv/vs/vs.vcxproj` — ARM64 Release+Debug configs now link
  against split `opencv_*4.lib` files instead of `opencv_world4120.lib`
- `core/release/deploy_windows.cmd` — ARM64 opencv copy uses `opencv_*4.dll` wildcard
- `.github/workflows/release-build.yml` — CI copies `opencv_*4.dll` from vcpkg
- `core/lib/opencv/win/arm64/lib/opencv_world4120.lib` — deleted (obsolete)
