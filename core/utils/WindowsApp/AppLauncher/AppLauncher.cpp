// Modernized AppLauncher.cpp with Windows 11 Look & Feel Enhancements
#include "framework.h"
#include "AppLauncher.h"

#include <gdiplus.h>
#include <commctrl.h>

#pragma comment (lib,"Gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

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
    BOOL enabled = TRUE;
    DWM_SYSTEMBACKDROP_TYPE backdropType = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));

    return TRUE;
}

HFONT CreateModernFont() {
    return CreateFont(0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable");
}

void ApplyModernStyle(HWND hwnd) {
    SendMessage(hwnd, WM_SETFONT, (WPARAM)CreateModernFont(), TRUE);
    SetWindowTheme(hwnd, L"Explorer", NULL);
}

void ApplyButtonIcon(HWND hwnd, HICON hIcon) {
    SendMessage(hwnd, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hIcon);
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

#include <gdiplus.h>
#include <atlbase.h> // for CComPtr, CComStream

using namespace Gdiplus;
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

  const int wndWidth = 420;
  const int wndHeight = 645;
  const int buttonHeight = 82;
  const int padding = 35;

  bannerImage = LoadImageFromResource(hInst, IDC_APP_ICON, L"PNG");
  
  HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, wndWidth, wndHeight, nullptr,
                            nullptr, hInstance, nullptr);

  if(!hWnd) {
    return FALSE;
  }

  EnableMicaEffect(hWnd);

  // Load stock icon (e.g., command prompt icon)
  SHSTOCKICONINFO cmdIcon = { sizeof(cmdIcon) };
  if(!SHGetStockIconInfo(SIID_RENAME, SHGSI_ICON, &cmdIcon)) {
    HWND hWndCmdButton = CreateWindow(WC_BUTTON, L"Command Prompt", WS_VISIBLE | WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      10, 145, wndWidth - padding, buttonHeight, hWnd, (HMENU)CMD_BUTTON, hInstance, nullptr);
    ApplyModernStyle(hWndCmdButton);
    ApplyButtonIcon(hWndCmdButton, cmdIcon.hIcon);
  }

  SHSTOCKICONINFO apiIcon = { sizeof(apiIcon) };
  if(!SHGetStockIconInfo(SIID_DOCASSOC, SHGSI_ICON, &apiIcon)) {
    HWND hWndApiButton = CreateWindow(WC_BUTTON, L"API Documentation", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      10, buttonHeight * 1 + 155, wndWidth - padding, buttonHeight, hWnd, (HMENU)API_BUTTON, hInstance, nullptr);
    ApplyModernStyle(hWndApiButton);
    ApplyButtonIcon(hWndApiButton, apiIcon.hIcon);
  }

  SHSTOCKICONINFO exIcon = { sizeof(exIcon) };
  if(!SHGetStockIconInfo(SIID_AUTOLIST, SHGSI_ICON, &exIcon)) {
    HWND hWndExamplesButton = CreateWindow(WC_BUTTON, L"Code Examples", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           10, buttonHeight * 2 + 165, wndWidth - padding, buttonHeight, hWnd, (HMENU)EXAMPLE_BUTTON, hInstance, nullptr);
    ApplyModernStyle(hWndExamplesButton);
    ApplyButtonIcon(hWndExamplesButton, exIcon.hIcon);
  }

  SHSTOCKICONINFO editIcon = { sizeof(editIcon) };
  if(!SHGetStockIconInfo(SIID_STACK, SHGSI_ICON, &editIcon)) {
    HWND hWndEditorButton = CreateWindow(WC_BUTTON, L"Text Editor Support", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         10, buttonHeight * 3 + 175, wndWidth - padding, buttonHeight, hWnd, (HMENU)EDITOR_BUTTON, hInstance, nullptr);
    ApplyModernStyle(hWndEditorButton);
    ApplyButtonIcon(hWndEditorButton, editIcon.hIcon);
  }

  SHSTOCKICONINFO readIcon = { sizeof(readIcon) };
  if(!SHGetStockIconInfo(SIID_HELP, SHGSI_ICON, &readIcon)) {
    HWND hWndReadmeButton = CreateWindow(WC_BUTTON, L"Read Me", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         10, buttonHeight * 4 + 185, wndWidth - padding, buttonHeight, hWnd, (HMENU)README_BUTTON, hInstance, nullptr);
    ApplyModernStyle(hWndReadmeButton);
    ApplyButtonIcon(hWndReadmeButton, readIcon.hIcon);
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
  _beginthreadex(nullptr, 0, VersionCheck, hWnd, 0, nullptr);
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
      Graphics graphics(hdc);
      graphics.DrawImage(bannerImage, 60, 10, bannerImage->GetWidth(), bannerImage->GetHeight());
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

  // get current directory
  if(!GetCurrentDirectory(MAX_PATH - 1, buffer)) {
    return FALSE;
  }
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

  pathText += L"\r\n@echo Copyright(c) 2025, Randy Hollines\r\n@echo =========================================";
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

BOOL GetLatestVersion()
{
  const int bufferSize = 4096;

  HANDLE hInit = InternetOpen(L"Agent", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
  if(!hInit) {
    InternetCloseHandle(hInit);
    return -1;
  }

  HANDLE hOpen = InternetOpenUrl(hInit, L"https://www.objeck.org/doc/api/version.txt", 0, 0, INTERNET_FLAG_RAW_DATA, 0);
  if(!hOpen) {
    InternetCloseHandle(hInit);
    InternetCloseHandle(hInit);
    return -1;
  }

  CHAR buffer[bufferSize];
  ZeroMemory(&buffer, bufferSize);

  DWORD bytesRead;
  if(!InternetReadFile(hOpen, (LPVOID)buffer, bufferSize, &bytesRead)) {
    InternetCloseHandle(hOpen);
    InternetCloseHandle(hInit);
    return -1;
  }

  InternetCloseHandle(hOpen);
  InternetCloseHandle(hInit);

  return atoi(buffer);
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
  return writtenBytes = (DWORD)asciiText.size();
}