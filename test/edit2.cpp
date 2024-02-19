
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Comctl32.lib")

// this generates edit2.exe.manifest btw
// https://learn.microsoft.com/en-us/windows/win32/controls/cookbook-overview?redirectedfrom=MSDN
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


#define WINDOW 1000

#define OUTPUT 1001
#define INPUT  1002
#define SEND   1003

#define WIDTH (470)
#define HEIGHT (350)

#define OUTW (WIDTH-48+10)
#define OUTH (HEIGHT-176+10)

#define INW (WIDTH-64+10)
#define INH (HEIGHT-272+10)

HWND outputbox,inputbox;
HDC hdc;
// HFONT font=CreateFont(-17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FF_DONTCARE,"Courier New");
HFONT font =CreateFont(-16,0,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_OUTLINE_PRECIS,
                    CLIP_DEFAULT_PRECIS,PROOF_QUALITY , VARIABLE_PITCH,TEXT("Arial"));
// ANTIALIASED_QUALITY, CLEARTYPE_QUALITY
HINSTANCE hinstance;

LRESULT CALLBACK mainproc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam){
	switch(msg){
        case WM_PAINT:
        {
        RECT rect;
        HFONT hFontOriginal, hFont1, hFont2, hFont3;
        PAINTSTRUCT ps;
    HDC hdc;

        hdc = BeginPaint(hwnd, &ps);


            //Logical units are device dependent pixels, so this will create a handle to a logical font that is 48 pixels in height.
            //The width, when set to 0, will cause the font mapper to choose the closest matching value.
            //The font face name will be Impact.
            hFont1 = CreateFont(48,0,0,0,FW_DONTCARE,FALSE,TRUE,FALSE,DEFAULT_CHARSET,OUT_OUTLINE_PRECIS,
                CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY, VARIABLE_PITCH,TEXT("Impact"));
            hFontOriginal = (HFONT)SelectObject(hdc, hFont1);

            //Sets the coordinates for the rectangle in which the text is to be formatted.
            SetRect(&rect, 20,20,80,80);
            SetTextColor(hdc, RGB(255,0,0));
            DrawText(hdc, TEXT("Drawing Text with Impact"), -1,&rect, DT_NOCLIP);

            //Logical units are device dependent pixels, so this will create a handle to a logical font that is 36 pixels in height.
            //The width, when set to 20, will cause the font mapper to choose a font which, in this case, is stretched.
            //The font face name will be Times New Roman.  This time nEscapement is at -300 tenths of a degree (-30 degrees)
            hFont2 = CreateFont(36,20,0,0,FW_DONTCARE,FALSE,TRUE,FALSE,DEFAULT_CHARSET,OUT_OUTLINE_PRECIS,
                CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY, VARIABLE_PITCH,TEXT("Times New Roman"));
            SelectObject(hdc,hFont2);

            //Sets the coordinates for the rectangle in which the text is to be formatted.
            SetRect(&rect, 39, 40, 80, 80);
            SetTextColor(hdc, RGB(0,128,0));
            DrawText(hdc, TEXT("Drawing Text with Times New Roman"), -1,&rect, DT_NOCLIP);

            //Logical units are device dependent pixels, so this will create a handle to a logical font that is 36 pixels in height.
            //The width, when set to 10, will cause the font mapper to choose a font which, in this case, is compressed.
            //The font face name will be Arial. This time nEscapement is at 250 tenths of a degree (25 degrees)
            hFont3 = CreateFont(36,10,0,0,FW_DONTCARE,FALSE,TRUE,FALSE,DEFAULT_CHARSET,OUT_OUTLINE_PRECIS,
                CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY, VARIABLE_PITCH,TEXT("Arial"));
            SelectObject(hdc,hFont3);

            //Sets the coordinates for the rectangle in which the text is to be formatted.
            SetRect(&rect, 60, 60, 1400, 600);
            SetTextColor(hdc, RGB(0,0,255));
            DrawText(hdc, TEXT("Drawing Text with Arial"), -1,&rect, DT_NOCLIP);

            SelectObject(hdc,hFontOriginal);
            DeleteObject(hFont1);
            DeleteObject(hFont2);
            DeleteObject(hFont3);

        EndPaint(hwnd, &ps);
        break;
        }


		case WM_CREATE:
        {
            NONCLIENTMETRICS ncm;
            ncm.cbSize = sizeof(ncm);

            // If we're compiling with the Vista SDK or later, the NONCLIENTMETRICS struct
            // will be the wrong size for previous versions, so we need to adjust it.
            // #if(_MSC_VER >= 1500 && WINVER >= 0x0600)
            // if (!SystemInfo::IsVistaOrLater())
            // {
            //     // In versions of Windows prior to Vista, the iPaddedBorderWidth member
            //     // is not present, so we need to subtract its size from cbSize.
            //     ncm.cbSize -= sizeof(ncm.iPaddedBorderWidth);
            // }
            // #endif

            SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
            HFONT hDlgFont = CreateFontIndirect(&(ncm.lfMessageFont));
            font = hDlgFont;
			outputbox = CreateWindow("static",">Please enter your username",WS_VISIBLE|WS_CHILD,16,16,OUTW,OUTH,hwnd,NULL,hinstance,NULL);
			inputbox  = CreateWindow("edit","",WS_VISIBLE|WS_CHILD|ES_MULTILINE|ES_AUTOVSCROLL,16,OUTH+32,INW,INH,hwnd,NULL,hinstance,NULL);
			SendMessage(outputbox,WM_SETFONT,(WPARAM)font,MAKELPARAM(true,0));
			SendMessage(inputbox,WM_SETFONT,(WPARAM)font,MAKELPARAM(true,0));
            SetFocus(inputbox);
			break;
        }
		case WM_CTLCOLORSTATIC:
			SetTextColor((HDC)wparam,RGB(0,255,0));
			SetBkMode((HDC)wparam,TRANSPARENT);
			return (LRESULT)GetStockObject(BLACK_BRUSH);
		case WM_CTLCOLOREDIT:
			// SetTextColor((HDC)wparam,RGB(0,255,0));
			// SetBkMode((HDC)wparam,TRANSPARENT);

            SetTextColor((HDC)wparam,RGB(0,255,0));
			SetBkColor((HDC)wparam,RGB(0,0,0));     // Set background color too
			SetBkMode((HDC)wparam,OPAQUE); // the background MUST be opaque



			return (LRESULT)GetStockObject(BLACK_BRUSH);
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;

            }
	return DefWindowProc(hwnd,msg,wparam,lparam);
    }

//main

int WINAPI WinMain(HINSTANCE hinst,HINSTANCE hprev,char* cmdparam,int cmdshow){
	//variable declarations
	HWND hwnd;
	MSG messages;
	WNDCLASSEX wndclass;

    // https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process
//     <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
// <assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0" xmlns:asmv3="urn:schemas-microsoft-com:asm.v3">
//   <asmv3:application>
//     <asmv3:windowsSettings>
//       <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true</dpiAware>
//       <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
//     </asmv3:windowsSettings>
//   </asmv3:application>
// </assembly>

    SetProcessDPIAware();
	//register window class
	wndclass.hInstance=hinst;
	wndclass.lpszClassName="MyClass";
	wndclass.lpfnWndProc=mainproc;
	wndclass.style=CS_DBLCLKS;
	wndclass.cbSize=sizeof(WNDCLASSEX);
	wndclass.hIcon=LoadIcon(NULL,IDI_APPLICATION);
	wndclass.hIconSm=LoadIcon(NULL,IDI_APPLICATION);
	wndclass.hCursor=LoadCursor(NULL,IDC_ARROW);
	wndclass.lpszMenuName=NULL;
	wndclass.cbClsExtra=0;
	wndclass.cbWndExtra=0;
	wndclass.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
	if(!RegisterClassEx(&wndclass)){
		MessageBox(0,"ERROR: Failed to register window class","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 1;}
	//create the window
	hwnd=CreateWindowEx(0,"MyClass","MyApp",WS_OVERLAPPEDWINDOW&~WS_THICKFRAME,CW_USEDEFAULT,CW_USEDEFAULT,WIDTH,HEIGHT,HWND_DESKTOP,NULL,hinst,NULL);
	if(!hwnd){
		MessageBox(0,"ERROR: Failed to create window","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 2;}
	ShowWindow(hwnd,cmdshow);
	//enter main loop
	while(GetMessage(&messages,NULL,0,0)){
		TranslateMessage(&messages);
		DispatchMessage(&messages);}
	return messages.wParam;}
