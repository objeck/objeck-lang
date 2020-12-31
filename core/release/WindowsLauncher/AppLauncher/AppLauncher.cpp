#include "framework.h"
#include "AppLauncher.h"

#define MAX_LOADSTRING 256

#define CMD_BUTTON 201
#define API_BUTTON 202 
#define EXAMPLE_BUTTON 203
#define README_BUTTON 204
#define EDITOR_BUTTON 205
#define CLOSE_BUTTON 206
#define VERSION_TIMER 207

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

std::wstring applicationPath;                   // application path
std::wstring programDataPath;                   // program data path

// Forward declarations of functions included in this code module:
ATOM RegisterWndClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK VersionProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
VOID CALLBACK VersionCheckProc(
  HWND hwnd,        // handle to window for timer messages 
  UINT message,     // WM_TIMER message 
  UINT idTimer,     // timer identifier 
  DWORD dwTime);     // current system time 

BOOL InitEnvironment();
BOOL WriteLineToFile(HANDLE file, std::wstring text);
int GetLatestVersion();
int GetLocalVersion();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR    lpCmdLine, _In_ int nCmdShow)
{
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  // Initialize global strings
  LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
  LoadStringW(hInstance, IDC_WINDOWSTEST, szWindowClass, MAX_LOADSTRING);
  RegisterWndClass(hInstance);

  // Perform application initialization:
  if(!InitInstance (hInstance, nCmdShow)) {
      return FALSE;
  }

  HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINDOWSTEST));

  // Main message loop:
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
      if(!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
      }
  }

  return (int)msg.wParam;
}

ATOM RegisterWndClass(HINSTANCE hInstance)
{
  WNDCLASSEXW wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style          = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc    = WndProc;
  wcex.cbClsExtra     = 0;
  wcex.cbWndExtra     = 0;
  wcex.hInstance      = hInstance;
  wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDC_APP_ICON));
  wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
  wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_WINDOWSTEST);
  wcex.lpszClassName  = szWindowClass;
  wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDC_APP_ICON));

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
  } 
  while(Process32Next(hProcessSnap, &pe32));

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
  } 
  while(curWnd != nullptr);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
hInst = hInstance;

  const int wndWidth = 450; 
  const int wndHeight = 560;
  const int buttonHeight = 82;

  // check for an existing instance
  std::vector <HWND> hWnds;
  GetWindowsFromProcessId(FindProcessId(L"ObLauncher.exe"), hWnds);
  if(hWnds.size()) {
    for(size_t i = 0; i < hWnds.size(); ++i) {
      HWND hOtherWnd = hWnds[i];
      SetForegroundWindow(hOtherWnd);
      if(IsIconic(hOtherWnd)) {
        ShowWindow(hOtherWnd, SW_RESTORE);
      }
    }

    return FALSE;
  }
 

  HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, wndWidth, wndHeight, nullptr,
                            nullptr, hInstance, nullptr);

  const int padding = 35;
  HWND hWndCmdButton = CreateWindow(WC_BUTTON, L"Command Prompt",
                                    BS_DEFCOMMANDLINK | WS_CHILD | WS_VISIBLE,
                                    10, 10, wndWidth - padding, buttonHeight,
                                    hWnd, (HMENU)CMD_BUTTON, hInstance, nullptr);

  HWND hWndApiButton = CreateWindow(WC_BUTTON, L"API Documentation",
                                    BS_COMMANDLINK | WS_CHILD | WS_VISIBLE,
                                    10, buttonHeight * 1 + 10 * 2, wndWidth - padding, buttonHeight,
                                    hWnd, (HMENU)API_BUTTON, hInstance, nullptr);

  HWND hWndExamplesButton = CreateWindow(WC_BUTTON, L"Code Examples",
                                        BS_COMMANDLINK | WS_CHILD | WS_VISIBLE,
                                        10, buttonHeight * 2 + 10 * 3, wndWidth - padding, buttonHeight,
                                        hWnd, (HMENU)EXAMPLE_BUTTON, hInstance, nullptr);

  HWND hWndEditorButton = CreateWindow(WC_BUTTON, L"Text Editor Support",
                                       BS_COMMANDLINK | WS_CHILD | WS_VISIBLE,
                                       10, buttonHeight * 3 + 10 * 4, wndWidth - padding, buttonHeight,
                                       hWnd, (HMENU)EDITOR_BUTTON, hInstance, nullptr);

  HWND hWndReadmeButton = CreateWindow(WC_BUTTON, L"Read Me",
                                      BS_COMMANDLINK | WS_CHILD | WS_VISIBLE,
                                      10, buttonHeight * 4 + 10 * 5, wndWidth - padding, buttonHeight,
                                      hWnd, (HMENU)README_BUTTON, hInstance, nullptr);

  const int closeButtonWidth = 80;
  HWND hWndCloseButton = CreateWindow(WC_BUTTON, L"Close",
                                      WS_CHILD | WS_VISIBLE,
                                      wndWidth / 2 - closeButtonWidth / 2, 480, closeButtonWidth, 24,
                                      hWnd, (HMENU)CLOSE_BUTTON, hInstance, nullptr);

  if(!hWnd || !hWndCmdButton || !hWndApiButton || !hWndExamplesButton || !hWndReadmeButton) {
    return FALSE;
  }

  HINSTANCE hShellDll = LoadLibrary(L"SHELL32.dll");

  // hWndCmdButton
  SendMessage(hWndCmdButton, BCM_SETNOTE, 0, (LPARAM)L"Objeck command prompt environment.\r\n(Alt+Shift+C)");
  HICON hIcon = LoadIcon(hShellDll, MAKEINTRESOURCE(242));
  SendMessageW(hWndCmdButton, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIcon);
   
  // hWndApiButton
  SendMessage(hWndApiButton, BCM_SETNOTE, 0, (LPARAM)L"Documentation for bundles and supporting classes.\r\n(Alt+Shift+D)");
  hIcon = LoadIcon(hShellDll, MAKEINTRESOURCE(134));
  SendMessageW(hWndApiButton, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIcon);
   
  // hWndExamplesButton
  SendMessage(hWndExamplesButton, BCM_SETNOTE, 0, (LPARAM)L"Sample code examples, copy locally to modify");
  hIcon = LoadIcon(hShellDll, MAKEINTRESOURCE(147));
  SendMessageW(hWndExamplesButton, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIcon);

  // hWndEditorButton
  SendMessage(hWndEditorButton, BCM_SETNOTE, 0, (LPARAM)L"Text editor support for syntax highlighting and compiling code.");
  hIcon = LoadIcon(hShellDll, MAKEINTRESOURCE(133));
  SendMessageW(hWndEditorButton, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIcon);

  // hWndReadmeButton
  SendMessage(hWndReadmeButton, BCM_SETNOTE, 0, (LPARAM)L"Information about this release and getting started.");
  hIcon = LoadIcon(hShellDll, MAKEINTRESOURCE(1001));
  SendMessageW(hWndReadmeButton, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIcon);

  // 30-second timer
  UINT_PTR uResult = SetTimer(hWnd, VERSION_TIMER, /*30000*/10000, (TIMERPROC)VersionCheckProc);
  if(!uResult) {
    return FALSE;
  }

  // show window
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
/*
    else if(LOWORD(wParam) == IDC_CHECK1) {
      if(HIWORD(wParam) == BN_CLICKED) {
        if(SendDlgItemMessage(hDlg, IDC_CHECK1, BM_GETCHECK, 0, 0)) {
          MessageBox(NULL, L"Checkbox Selected", L"Success", MB_OK | MB_ICONINFORMATION);
        }
        else {
          MessageBox(NULL, L"Checkbox Unselected", L"Success", MB_OK | MB_ICONINFORMATION);
        }
      }
      return (INT_PTR)TRUE;
    }
*/
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
  switch (message) {
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
    PostQuitMessage(0);
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
  pathText += L")\r\n@echo Copyright(c) 2008-2021, Randy Hollines\r\n@echo =========================================";
  WriteLineToFile(cmdFile, pathText);

  pathText = L"set PATH=%PATH%;" + applicationPath + L"\\..\\bin;" + applicationPath + L"\\..\\lib\\sdl";
  WriteLineToFile(cmdFile, pathText);

  pathText = L"set OBJECK_LIB_PATH=" + applicationPath + L"\\..\\lib";
  WriteLineToFile(cmdFile, pathText);

  pathText = L"title Objeck Prompt";
  WriteLineToFile(cmdFile, pathText);

  pathText = L"cd ..";
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