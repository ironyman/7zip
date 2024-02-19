#include <windows.h>
#include <commctrl.h> // Include the common controls header

// Global variables
HWND g_hListView;
HWND g_hEdit;

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "Comctl32.lib")

// Function to create the main window and child controls
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
    HWND hWnd = CreateWindowEx(0, "STATIC", "Parent Window", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        return FALSE;
    }

    // Create list view control
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    g_hListView = CreateWindow(WC_LISTVIEW, "", WS_CHILD | WS_VISIBLE | LVS_REPORT,
        10, 10, 200, 200, hWnd, NULL, hInstance, NULL);

    // Create edit control
    g_hEdit = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        10, 220, 200, 20, hWnd, NULL, hInstance, NULL);

    // Set extended style for list view to accept notifications
    ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT);

    // Populate list view with some items for demonstration
    LVITEM lvItem = { 0 };
    lvItem.mask = LVIF_TEXT;
    lvItem.iItem = 0;
    lvItem.iSubItem = 0;
    lvItem.pszText = "Item 1";
    ListView_InsertItem(g_hListView, &lvItem);
    lvItem.iItem = 1;
    lvItem.pszText = "Item 2";
    ListView_InsertItem(g_hListView, &lvItem);
    // Add more items as needed

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

// Function to handle WM_NOTIFY messages
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NOTIFY:
        {
            NMHDR* pnmhdr = (NMHDR*)lParam;
            if (pnmhdr->hwndFrom == g_hListView && pnmhdr->code == NM_CUSTOMDRAW) {
                // Adjust edit control position when list view is scrolled
                RECT rcList;
                GetWindowRect(g_hListView, &rcList);
                MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcList, 2);
                SetWindowPos(g_hEdit, HWND_TOP, rcList.left + 10, rcList.bottom + 10, 0, 0, SWP_NOSIZE);
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

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register window class
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = "MyClass";
    wcex.hIconSm = NULL;

    if (!RegisterClassEx(&wcex)) {
        MessageBox(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Initialize and display main window
    if (!InitInstance(hInstance, nCmdShow)) {
        MessageBox(NULL, "Window Initialization Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
