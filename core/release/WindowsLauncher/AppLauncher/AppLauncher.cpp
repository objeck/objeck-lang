#include "framework.h"
#include "AppLauncher.h"

#define MAX_LOADSTRING 100

#define CMD_BUTTON 201
#define API_BUTTON 202 
#define EXAMPLE_BUTTON 203
#define README_BUTTON 204
#define CLOSE_BUTTON 205

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM RegisterWndClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

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

  MSG msg;

  // Main message loop:
  while (GetMessage(&msg, nullptr, 0, 0)) {
      if(!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
      }
  }

  return (int) msg.wParam;
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

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
hInst = hInstance;

  const int wndWidth = 450; 
  const int wndHeight = 400;

  HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPED | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT, wndWidth, wndHeight, nullptr,
                            nullptr, hInstance, nullptr);

  const int padding = 35;
  HWND hWndCmdButton = CreateWindow(WC_BUTTON, L"Command Prompt",
                                    BS_DEFCOMMANDLINK | WS_CHILD | WS_VISIBLE,
                                    10, 10, wndWidth - padding, 64,
                                    hWnd, (HMENU)CMD_BUTTON, hInstance, nullptr);

  HWND hWndApiButton = CreateWindow(WC_BUTTON, L"API Documentation",
                                    BS_COMMANDLINK | WS_CHILD | WS_VISIBLE,
                                    10, 85, wndWidth - padding, 64,
                                    hWnd, (HMENU)API_BUTTON, hInstance, nullptr);

  HWND hWndExamplesButton = CreateWindow(WC_BUTTON, L"Code Examples",
                                        BS_COMMANDLINK | WS_CHILD | WS_VISIBLE,
                                        10, 160, wndWidth - padding, 64,
                                        hWnd, (HMENU)EXAMPLE_BUTTON, hInstance, nullptr);

  HWND hWndReadmeButton = CreateWindow(WC_BUTTON, L"Read Me",
                                      BS_COMMANDLINK | WS_CHILD | WS_VISIBLE,
                                      10, 235, wndWidth - padding, 64,
                                      hWnd, (HMENU)README_BUTTON, hInstance, nullptr);

  const int closeButtonWidth = 80;
  HWND hWndCloseButton = CreateWindow(WC_BUTTON, L"Close",
                                      WS_CHILD | WS_VISIBLE,
                                      wndWidth / 2 - closeButtonWidth / 2, 235 + 82, closeButtonWidth, 30,
                                      hWnd, (HMENU)CLOSE_BUTTON, hInstance, nullptr);

  if(!hWnd || !hWndCmdButton || !hWndApiButton || !hWndExamplesButton || !hWndReadmeButton) {
    return FALSE;
  }

  HINSTANCE hShellDll = LoadLibrary(L"SHELL32.dll");

  // hWndCmdButton
  SendMessage(hWndCmdButton, BCM_SETNOTE, 0, (LPARAM)L"Command prompt for Objeck binaries.");
  HICON hIcon = LoadIcon(hShellDll, MAKEINTRESOURCE(242));
  SendMessageW(hWndCmdButton, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIcon);
   
  // hWndApiButton
  SendMessage(hWndApiButton, BCM_SETNOTE, 0, (LPARAM)L"Documentation for bundles and supporting classes.");
  hIcon = LoadIcon(hShellDll, MAKEINTRESOURCE(134));
  SendMessageW(hWndApiButton, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIcon);
   
  // hWndExamplesButton
  SendMessage(hWndExamplesButton, BCM_SETNOTE, 0, (LPARAM)L"Directory of code examples, copy locally and modify");
  hIcon = LoadIcon(hShellDll, MAKEINTRESOURCE(147));
  SendMessageW(hWndExamplesButton, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIcon);

  // hWndReadmeButton
  SendMessage(hWndReadmeButton, BCM_SETNOTE, 0, (LPARAM)L"Information about the release and getting started.");
  hIcon = LoadIcon(hShellDll, MAKEINTRESOURCE(1001));
  SendMessageW(hWndReadmeButton, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIcon);

  // show window
  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef _DEBUG
  const LPCWCHAR currentPath = L"\"..\\..\\deploy64\\";
#else
  WCHAR currentPath[MAX_PATH];
  GetCurrentDirectory(MAX_PATH - 1, currentPath);
#endif


  switch (message) {
  case WM_COMMAND: {
    const int wmId = LOWORD(wParam);
    // Parse the menu selections:
    switch(wmId) {
    case CMD_BUTTON: {
      std::wstring command = L"/k ";
      command += currentPath;
      command += L"app\\set_ob_env.cmd\"";
      ShellExecute(nullptr, L"open", L"cmd.exe", command.c_str(), nullptr, SW_SHOWDEFAULT);
    }
      break;

    case API_BUTTON: {
      std::wstring command = currentPath;
      command += L"doc\\api\\index.html\"";
      ShellExecute(nullptr, L"open", command.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }
      break;

    case EXAMPLE_BUTTON: {
      std::wstring command = currentPath;
      command += L"examples\"";
      ShellExecute(nullptr, L"open", command.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }
      break;

    case README_BUTTON: {
      std::wstring command = currentPath;
      command += L"readme.html\"";
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