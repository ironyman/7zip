#include <windows.h>
#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "Comctl32.lib")

// https://learn.microsoft.com/en-us/windows/win32/controls/cookbook-overview?redirectedfrom=MSDN
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  MSG msg;
  WNDCLASSW wc = {0};
  wc.lpszClassName = L"Edit control";
  wc.hInstance = hInstance;
  wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
  wc.lpfnWndProc = WndProc;
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  RegisterClassW(&wc);

  CreateWindowW(wc.lpszClassName, L"Edit control",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 220, 220, 280, 200, 0, 0,
                hInstance, 0);

  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  static HWND hwndEdit;
  HWND hwndButton;
  switch (msg) {
  case WM_CREATE:

    hwndEdit = CreateWindowW(L"Edit", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER,
                             50, 50, 150, 20, hwnd, (HMENU)102, NULL, NULL);

    hwndButton = CreateWindowW(L"button", L"Set title", WS_VISIBLE | WS_CHILD,
                               50, 100, 80, 25, hwnd, (HMENU)103, NULL, NULL);

    break;

  case WM_COMMAND:

    if (HIWORD(wParam) == BN_CLICKED) {

      int len = GetWindowTextLengthW(hwndEdit) + 1;
      wchar_t text[999];

      GetWindowTextW(hwndEdit, text, len);
      SetWindowTextW(hwnd, text);
    }

    break;

  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}