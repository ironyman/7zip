#include<Windows.h>
#include <commctrl.h >
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib,"comctl32.lib")

HWND hwndEdit;
LRESULT CALLBACK WndProc(HWND hWnd,UINT uMsg,WPARAM wp,LPARAM lp)
{
switch(uMsg)
{
case WM_CREATE:
    hwndEdit = CreateWindow(
            "EDIT",     /* predefined class                  */
            NULL,       /* no window title                   */
            WS_CHILD | WS_VISIBLE |
            ES_LEFT | ES_AUTOHSCROLL|WS_BORDER,
            0, 0, 100, 50, /* set size in WM_SIZE message       */
            hWnd,       /* parent window                     */
            (HMENU) 1, /* edit control ID         */
            (HINSTANCE) GetWindowLong(hWnd, GWL_HINSTANCE),
            NULL);                /* pointer not needed     */
    return 0;
    break;
case WM_CLOSE:
    ::PostQuitMessage(0);//quit application
    break;
default:
    return ::DefWindowProcA(hWnd,uMsg,wp,lp);
  }
 return 0l;
 }
 int WINAPI WinMain(HINSTANCE hinstance,HINSTANCE hPrevinstance,char *cmd,int show)
 {
   INITCOMMONCONTROLSEX icc;
   icc.dwICC=ICC_ANIMATE_CLASS|ICC_NATIVEFNTCTL_CLASS|ICC_STANDARD_CLASSES;
   icc.dwSize=sizeof(icc);
   InitCommonControlsEx(&icc);
   char* tst="Simple edit control";

   WNDCLASSEX mywindow;
   MSG msg;
   HWND hwnd;
   mywindow.cbClsExtra=0;
   mywindow.cbWndExtra=0;
   mywindow.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
   mywindow.hCursor=LoadCursor(NULL,IDC_CROSS);
   mywindow.hIcon=LoadIcon(NULL,IDI_APPLICATION);
   mywindow.hInstance=hinstance;
   mywindow.lpfnWndProc=WndProc;
   mywindow.lpszClassName="Test";
   mywindow.lpszMenuName=NULL;
   mywindow.style=0;
   mywindow.cbSize=sizeof(WNDCLASSEX);
   mywindow.hIconSm=NULL;

if(!RegisterClassEx(&mywindow))
    MessageBox(NULL,"Window Registration failed","Error occured",NULL);

hwnd=CreateWindowEx(WS_EX_TOPMOST,"Test","My window",WS_OVERLAPPEDWINDOW,900,300,400,350,NULL,NULL,hinstance,tst);
if(hwnd==NULL)
    MessageBox(NULL,"Window creation failed","error",NULL);

::ShowWindow(hwnd,SW_SHOW);
::UpdateWindow(hwnd);

while (1)                  //NOTE: Game engine type message loop
{ Sleep(1);
    if ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) )
    {
        if (msg.message == WM_QUIT)
            break;
        TranslateMessage( &msg );
        DispatchMessage ( &msg );

    }
}
return msg.wParam;
}
