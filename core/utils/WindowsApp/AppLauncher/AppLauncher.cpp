// Modernized AppLauncher.cpp with Windows 11 Look & Feel Enhancements
#include "framework.h"
#include "AppLauncher.h"

#include <gdiplus.h>
#include <commctrl.h>
#include <winhttp.h>

#pragma comment (lib,"Gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;

#define MAX_LOADSTRING 256
#define CMD_BUTTON 201
#define API_BUTTON 202 
#define EXAMPLE_BUTTON 203
#define README_BUTTON 204
#define EDITOR_BUTTON 205
#define CLOSE_BUTTON 206
#define VERSION_TIMER 207

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
std::wstring applicationPath;
std::wstring programDataPath;
Image* bannerImage;
UINT g_dpi = 96;  // DPI of the launcher window; set once the window exists

// scale a logical (96-DPI) measurement to the window's actual DPI
static int Scale(int value, UINT dpi) { return MulDiv(value, dpi, 96); }

// shared layout metrics in logical (96-DPI) units; used by InitInstance
// (window/button layout) and WM_PAINT (banner) so they stay aligned
static const int kMargin     = 18;
static const int kBannerTop  = 18;
static const int kBannerBand = 120;

ATOM RegisterWndClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK VersionProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
VOID CALLBACK VersionCheckProc(HWND hwnd, UINT message, UINT idTimer, DWORD dwTime);
BOOL InitEnvironment();
BOOL WriteLineToFile(HANDLE file, std::wstring text);
int GetLatestVersion();
int GetLocalVersion();
ULONG_PTR gdiToken;

void InitGDIPlus() {
  GdiplusStartupInput gdiplusStartupInput;
  GdiplusStartup(&gdiToken, &gdiplusStartupInput, nullptr);
}
void ShutdownGDIPlus() {
  GdiplusShutdown(gdiToken);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR    lpCmdLine, _In_ int nCmdShow)
{
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  // crisp rendering on scaled displays
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  InitGDIPlus();

  // Initialize global strings
  LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
  LoadStringW(hInstance, IDC_WINDOWSTEST, szWindowClass, MAX_LOADSTRING);
  RegisterWndClass(hInstance);

  // Perform application initialization:
  if(!InitInstance(hInstance, nCmdShow)) {
    return FALSE;
  }

  HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINDOWSTEST));

  // Main message loop:
  MSG msg;
  while(GetMessage(&msg, nullptr, 0, 0)) {
    if(!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  const int ret = (int)msg.wParam;
  ShutdownGDIPlus();

  return ret;
}

BOOL EnableMicaEffect(HWND hwnd) {
    DWM_SYSTEMBACKDROP_TYPE backdropType = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));

    // match the system light/dark titlebar preference
    DWORD appsUseLight = 1, valueSize = sizeof(appsUseLight);
    RegGetValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &appsUseLight, &valueSize);
    BOOL darkMode = appsUseLight ? FALSE : TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    return TRUE;
}

// one shared UI font for every control (was leaking an HFONT per button).
// Created lazily so it picks up g_dpi, which InitInstance sets before the
// first control is made; an 11pt font keeps the button labels readable at
// any display scale.
HFONT GetModernFont() {
    static HFONT font = CreateFontW(-MulDiv(11, g_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable");
    return font;
}

void ApplyModernStyle(HWND hwnd) {
    SendMessage(hwnd, WM_SETFONT, (WPARAM)GetModernFont(), TRUE);
    SetWindowTheme(hwnd, L"Explorer", NULL);
}

void ApplyButtonIcon(HWND hwnd, HICON hIcon) {
    // Use a button image list aligned left (BM_SETIMAGE centers the icon+text
    // group, which looks off in a left-aligned list). Size the list to the
    // icon's real pixels so it isn't clipped or padded.
    int cx = GetSystemMetrics(SM_CXICON), cy = GetSystemMetrics(SM_CYICON);
    ICONINFO ii = {};
    if(GetIconInfo(hIcon, &ii)) {
        BITMAP bm = {};
        if(GetObject(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm)) {
            cx = bm.bmWidth;
            cy = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;  // mask is double-height
        }
        if(ii.hbmColor) DeleteObject(ii.hbmColor);
        if(ii.hbmMask) DeleteObject(ii.hbmMask);
    }

    HIMAGELIST himl = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 1, 0);
    ImageList_AddIcon(himl, hIcon);

    BUTTON_IMAGELIST bil = {};
    bil.himl = himl;
    bil.uAlign = BUTTON_IMAGELIST_ALIGN_LEFT;
    bil.margin = { Scale(kMargin, g_dpi), 0, Scale(10, g_dpi), 0 };  // pad left edge, gap before text
    Button_SetImageList(hwnd, &bil);
}

ATOM RegisterWndClass(HINSTANCE hInstance)
{
  WNDCLASSEXW wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDC_APP_ICON));
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_WINDOWSTEST);
  wcex.lpszClassName = szWindowClass;
  wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDC_APP_ICON));

  return RegisterClassExW(&wcex);
}

DWORD FindProcessId(const wchar_t* processname)
{
  HANDLE hProcessSnap;
  PROCESSENTRY32 pe32;

  DWORD result = 0;
  hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if(INVALID_HANDLE_VALUE == hProcessSnap) {
    return FALSE;
  }

  pe32.dwSize = sizeof(PROCESSENTRY32);
  if(!Process32First(hProcessSnap, &pe32)) {
    CloseHandle(hProcessSnap);
    return 0;
  }

  do {
    if(!wcscmp(processname, pe32.szExeFile)) {
      result = pe32.th32ProcessID;
      break;
    }
  } while(Process32Next(hProcessSnap, &pe32));

  CloseHandle(hProcessSnap);
  return result;
}

void GetWindowsFromProcessId(DWORD pId, std::vector <HWND>& hWnds)
{
  HWND curWnd = nullptr;
  do {
    curWnd = FindWindowEx(nullptr, curWnd, nullptr, nullptr);
    DWORD checkId = 0;
    GetWindowThreadProcessId(curWnd, &checkId);
    if(checkId == pId) {
      hWnds.push_back(curWnd);
    }
  } while(curWnd != nullptr);
}

using namespace std;

// Helper to load a GDI+ Image from resource
Image* LoadImageFromResource(HINSTANCE hInstance, UINT resourceID, LPCWSTR resourceType) {
  HRSRC hResource = FindResource(hInstance, MAKEINTRESOURCE(resourceID), resourceType);
  if(!hResource) return nullptr;

  DWORD imageSize = SizeofResource(hInstance, hResource);
  HGLOBAL hGlobal = LoadResource(hInstance, hResource);
  if(!hGlobal) return nullptr;

  LPVOID pResourceData = LockResource(hGlobal);
  if(!pResourceData) return nullptr;

  // Copy to a memory stream
  HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
  if(!hBuffer) return nullptr;

  void* pBuffer = GlobalLock(hBuffer);
  memcpy(pBuffer, pResourceData, imageSize);
  GlobalUnlock(hBuffer);

  IStream* pStream = nullptr;
  CreateStreamOnHGlobal(hBuffer, TRUE, &pStream);
  if(!pStream) return nullptr;

  Image* image = new Image(pStream);
  pStream->Release();

  return image;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
  hInst = hInstance;

  bannerImage = LoadImageFromResource(hInst, IDC_APP_ICON, L"PNG");

  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

  // Create the window first so we can query the DPI of the monitor it landed
  // on, then size it precisely for that scale factor. The app is per-monitor
  // DPI aware, so fixed pixel sizes render tiny and cramped on high-DPI
  // displays (clipped title, overflowing banner, buttons below the edge).
  HWND hWnd = CreateWindowW(szWindowClass, szTitle, style,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                            nullptr, nullptr, hInstance, nullptr);

  if(!hWnd) {
    return FALSE;
  }

  const UINT dpi = GetDpiForWindow(hWnd);
  g_dpi = dpi;  // GetModernFont() reads this when it builds the shared font

  // buttons are always created; the stock icon is decoration and may be
  // unavailable (previously a failed icon lookup silently dropped the button)
  struct LauncherAction {
    LPCWSTR text;
    int id;
    SHSTOCKICONID icon;
  };
  const LauncherAction actions[] = {
    { L"Command Prompt",      CMD_BUTTON,     SIID_RENAME },
    { L"API Documentation",   API_BUTTON,     SIID_DOCASSOC },
    { L"Code Examples",       EXAMPLE_BUTTON, SIID_AUTOLIST },
    { L"Text Editor Support", EDITOR_BUTTON,  SIID_STACK },
    { L"Read Me",             README_BUTTON,  SIID_HELP },
  };

  // uniform vertical rhythm for the button stack (logical units)
  const int buttonHeight = 56;
  const int buttonGap    = 12;
  const int buttonTop    = kBannerTop + kBannerBand + 18;
  const int clientWidth  = 380;
  const int buttonCount  = ARRAYSIZE(actions);
  const int clientHeight = buttonTop + buttonCount * buttonHeight +
                           (buttonCount - 1) * buttonGap + 18;

  // resize so the CLIENT area is exactly clientWidth x clientHeight at this DPI
  RECT rc = { 0, 0, Scale(clientWidth, dpi), Scale(clientHeight, dpi) };
  AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);
  SetWindowPos(hWnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

  EnableMicaEffect(hWnd);

  int y = buttonTop;
  for(const LauncherAction& action : actions) {
    HWND hWndButton = CreateWindow(WC_BUTTON, action.text,
                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_LEFT | BS_VCENTER,
                                   Scale(kMargin, dpi), Scale(y, dpi),
                                   Scale(clientWidth - 2 * kMargin, dpi), Scale(buttonHeight, dpi),
                                   hWnd, (HMENU)(INT_PTR)action.id, hInstance, nullptr);
    ApplyModernStyle(hWndButton);

    SHSTOCKICONINFO icon = { sizeof(icon) };
    if(SUCCEEDED(SHGetStockIconInfo(action.icon, SHGSI_ICON | SHGSI_LARGEICON, &icon))) {
      ApplyButtonIcon(hWndButton, icon.hIcon);
    }
    y += buttonHeight + buttonGap;
  }

  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  return InitEnvironment();
}
INT_PTR CALLBACK VersionProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  UNREFERENCED_PARAMETER(lParam);

  switch(message) {
  case WM_INITDIALOG:
    return (INT_PTR)TRUE;

  case WM_NOTIFY:
    if(((LPNMHDR)lParam)->code == NM_CLICK) {
      ShellExecute(nullptr, L"open", L"https://www.objeck.org", nullptr, nullptr, SW_SHOWDEFAULT);
      return (INT_PTR)TRUE;
    }
    break;

  case WM_COMMAND:
    if(LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
      EndDialog(hDlg, LOWORD(wParam));
      return (INT_PTR)TRUE;
    }
    break;
  }

  return (INT_PTR)FALSE;
}

unsigned int WINAPI VersionCheck(LPVOID arg)
{
  HWND hWnd = (HWND)arg;

  // check for updates
  BOOL newVersion = FALSE;
  const int latestVersion = GetLatestVersion();
  if(latestVersion > 0) {
    const int localVersion = GetLocalVersion();
    if(localVersion > 0 && localVersion < latestVersion) {
      newVersion = TRUE;
    }
  }

  if(newVersion) {
    DialogBox(hInst, MAKEINTRESOURCE(IDD_DIALOG1), hWnd, VersionProc);
    newVersion = FALSE;
  }

  return 0;
}

VOID CALLBACK VersionCheckProc(HWND hWnd, UINT message, UINT idTimer, DWORD dwTime)
{
  HANDLE thread = (HANDLE)_beginthreadex(nullptr, 0, VersionCheck, hWnd, 0, nullptr);
  if(thread) {
    CloseHandle(thread); // fire and forget; don't leak the handle
  }
  KillTimer(hWnd, VERSION_TIMER);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch(message) {
  case WM_COMMAND: {
    const int wmId = LOWORD(wParam);
    // Parse the menu selections:
    switch(wmId) {
    case CMD_BUTTON: {
      std::wstring command = L"/k \""; // start
      command += programDataPath + L"\\SetObjEnv.cmd";
      command += L"\"";             // end
      ShellExecute(nullptr, L"open", L"cmd.exe", command.c_str(), nullptr, SW_SHOWDEFAULT);
    }
                   break;

    case API_BUTTON: {
      std::wstring command = L"\""; // start
      command += applicationPath + L"\\..\\doc\\api\\index.html";
      command += L"\"";             // end
      ShellExecute(nullptr, L"open", command.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }
                   break;

    case EXAMPLE_BUTTON: {
      std::wstring command = L"\""; // start
      command += applicationPath + L"\\..\\examples";
      command += L"\"";             // end
      ShellExecute(nullptr, L"open", command.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }
                       break;

    case EDITOR_BUTTON: {
      std::wstring command = L"\""; // start
      command += applicationPath + L"\\..\\doc\\syntax\\howto.html";
      command += L"\"";             // end
      ShellExecute(nullptr, L"open", command.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }
                      break;

    case README_BUTTON: {
      std::wstring command = L"\""; // start
      command += applicationPath + L"\\..\\readme.html";
      command += L"\"";             // end
      ShellExecute(nullptr, L"open", command.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }
                      break;

    case CLOSE_BUTTON:
    case IDM_EXIT:
      DestroyWindow(hWnd);
      break;

    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
  }
    break;

  case WM_DESTROY:
    if(bannerImage) {
      delete bannerImage;
      bannerImage = nullptr;
    }
    PostQuitMessage(0);
    break;

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    if(bannerImage) {
      RECT rc;
      GetClientRect(hWnd, &rc);
      const UINT dpi = GetDpiForWindow(hWnd);

      const int bandTop = Scale(kBannerTop, dpi);
      const int bandH   = Scale(kBannerBand, dpi);
      const int maxW    = rc.right - Scale(2 * kMargin, dpi);

      // fit the wordmark into its band, preserving aspect ratio, centered
      const UINT iw = bannerImage->GetWidth();
      const UINT ih = bannerImage->GetHeight();
      int h = bandH;
      int w = MulDiv(h, iw, ih);
      if(w > maxW) { w = maxW; h = MulDiv(w, ih, iw); }
      const int x = (rc.right - w) / 2;
      const int yImg = bandTop + (bandH - h) / 2;

      Graphics graphics(hdc);
      graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
      graphics.DrawImage(bannerImage, x, yImg, w, h);
    }

    EndPaint(hWnd, &ps);
  }
    break;

  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }

  return 0;
}

BOOL InitEnvironment()
{
  WCHAR buffer[MAX_PATH];

  // derive the install location from the executable itself; the working
  // directory depends on how the launcher was started (shortcut "Start in",
  // double-click, command line) and previously broke every relative path
  if(!GetModuleFileName(nullptr, buffer, MAX_PATH - 1)) {
    return FALSE;
  }
  PathRemoveFileSpec(buffer);
  applicationPath = buffer;

  // get program data directory
  if(SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, buffer) != S_OK) {
    return FALSE;
  }
  programDataPath = buffer;
  programDataPath += L"\\Objeck";

  // check for cmd file in program data directory
  if(!PathFileExists(programDataPath.c_str())) {
    if(!CreateDirectory(programDataPath.c_str(), nullptr)) {
      return FALSE;
    }
  }

  // create cmd file
  std::wstring programCmdFile = programDataPath + L"\\SetObjEnv.cmd";
  HANDLE cmdFile = CreateFile(programCmdFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if(cmdFile == INVALID_HANDLE_VALUE) {
    return FALSE;
  }

  // write to file
  std::wstring pathText;
  pathText = L"@echo off\r\n@echo =========================================\r\n@echo Objeck Command Prompt (v";
  pathText += VERSION_STRING;

#if defined(_WIN64) && defined(_WIN32) && defined(_M_ARM64)
  pathText += L", arm64 Windows)";
#elif defined(_WIN64) && defined(_WIN32)
  pathText += L", x86_64 Windows)";
#elif _WIN32
  pathText += L", x86 Windows)";
#elif _OSX
#ifdef _ARM64
  pathText += L", arm64 macOS)";
#else
  pathText += L", x86_64 macOS)";
#endif
#elif _X64
  pathText += L", x86_64 Linux)";
#elif _ARM32
  pathText += L", ARMv7 Linux)";
#else
  pathText += L", x86 Linux)";
#endif

  pathText += L"\r\n@echo Copyright(c) 2026, Randy Hollines\r\n@echo =========================================";
  WriteLineToFile(cmdFile, pathText);

  pathText = L"set PATH=%PATH%;" + applicationPath + L"\\..\\bin;" + applicationPath + L"\\..\\lib\\sdl";
  WriteLineToFile(cmdFile, pathText);

  pathText = L"set OBJECK_LIB_PATH=" + applicationPath + L"\\..\\lib";
  WriteLineToFile(cmdFile, pathText);

  pathText = L"title Objeck Prompt";
  WriteLineToFile(cmdFile, pathText);

  pathText = L"cd %HOMEDRIVE%\\%HOMEPATH%\\Documents";
  WriteLineToFile(cmdFile, pathText);

  pathText = L"%HOMEDRIVE%";
  WriteLineToFile(cmdFile, pathText);

  // close file
  CloseHandle(cmdFile);

  return TRUE;
}

int GetLatestVersion()
{
  // WinHTTP (WinINet is legacy for applications); every path releases its
  // handles exactly once
  int version = -1;

  HINTERNET hSession = WinHttpOpen(L"ObjeckLauncher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if(!hSession) {
    return -1;
  }
  WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

  HINTERNET hConnect = WinHttpConnect(hSession, L"www.objeck.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
  if(hConnect) {
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/doc/api/version.txt",
                                            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if(hRequest) {
      if(WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
         WinHttpReceiveResponse(hRequest, nullptr)) {
        CHAR buffer[64];
        ZeroMemory(buffer, sizeof(buffer));
        DWORD bytesRead = 0;
        if(WinHttpReadData(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
          version = atoi(buffer);
        }
      }
      WinHttpCloseHandle(hRequest);
    }
    WinHttpCloseHandle(hConnect);
  }
  WinHttpCloseHandle(hSession);

  return version;
}

int GetLocalVersion()
{
  const int bufferSize = 4096;

  std::wstring command = applicationPath + L"\\..\\doc\\api\\version.txt";
  HANDLE hFile = CreateFile(command.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if(hFile == INVALID_HANDLE_VALUE) {
    return -1;
  }

  CHAR buffer[bufferSize];
  ZeroMemory(&buffer, bufferSize);

  DWORD readBytes;
  if(!ReadFile(hFile, buffer, bufferSize - 1, &readBytes, nullptr)) {
    CloseHandle(hFile);
    return -1;
  }

  FlushFileBuffers(hFile);
  CloseHandle(hFile);

  return atoi(buffer);
}

BOOL WriteLineToFile(HANDLE file, std::wstring text)
{
  text += L"\r\n";
  DWORD writtenBytes;
  std::string asciiText = UnicodeToBytes(text);
  WriteFile(file, asciiText.c_str(), (DWORD)asciiText.size(), &writtenBytes, nullptr);
  return writtenBytes == (DWORD)asciiText.size();
}