#include "StdAfx.h"
#include <windowsx.h>
#include "PanelFind.h"
#include "Panel.h"
#include "Debounce.h"
#include "../../../Windows/Window.h"

bool CPanelFind::Create(PCWSTR text, HINSTANCE hInstance, CPanel *panel, HWND hwndParent, UINT id)
{
  this->_panel = panel;
  bool success = this->CreateEx(WS_EX_CLIENTEDGE, L"EDIT", text,
    WS_CHILD | WS_BORDER | ES_LEFT | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | ES_AUTOHSCROLL,
    0, 0, 0, 32, hwndParent, (HMENU)(ULONGLONG)id, hInstance);
  if (!success)
  {
    return success;
  }

  NONCLIENTMETRICS ncm;
  ncm.cbSize = sizeof(ncm);

  SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
  HFONT hFont = CreateFontIndirect(&ncm.lfMessageFont);

  SendMessage(*this, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(true,0));
  SetWindowProc();
  return success;
}

static LRESULT APIENTRY PanelFindEditSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  NWindows::CWindow window(hwnd);
  CPanelFind *w = (CPanelFind *)(window.GetUserDataLongPtr());
  if (w == NULL)
    return 0;
  return w->OnMessage(message, wParam, lParam);
}

LRESULT CPanelFind::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
    case WM_CHAR:
    {
      switch (wParam)
      {
        case VK_ESCAPE:
          _panel->ExitFindMode();
          // MessageBoxA(*this, "Escape key pressed!", "Key Press", MB_OK);
          // Return immediately to prevent audible bell.
          return 0;
        case VK_RETURN:
          if (NWindows::IsKeyDown(VK_CONTROL))
          {
            _panel->ExitFindMode();
            _panel->OnNotifyActivateItems();
          }
          else
          {
            int textLen = Edit_GetTextLength(*this);
            UString text;
            Edit_GetText(*this, text.GetBuf(textLen), textLen);
            _panel->FindNextItem(text);
          }
          return 0;
      }

      // LRESULT result = CallWindowProc(_origWindowProc, *this, message, wParam, lParam);
      // int textLen = Edit_GetTextLength(*this);
      // UString text;
      // Edit_GetText(*this, text.GetBuf(textLen), textLen);
      // _panel->FindNextItem(text);
      // return result;
    }
  }

  #ifndef _UNICODE
  if (g_IsNT)
    return CallWindowProcW(_origWindowProc, *this, message, wParam, lParam);
  else
  #endif
    return CallWindowProc(_origWindowProc, *this, message, wParam, lParam);
}

void CPanelFind::SetWindowProc()
{
  SetUserDataLongPtr((LONG_PTR)this);
  #ifndef _UNICODE
  if (g_IsNT)
    _origWindowProc = (WNDPROC)SetLongPtrW(GWLP_WNDPROC, (LONG_PTR)PanelFindEditSubclassProc);
  else
  #endif
    _origWindowProc = (WNDPROC)SetLongPtr(GWLP_WNDPROC, (LONG_PTR)PanelFindEditSubclassProc);
}

void CPanel::EnterFindMode()
{
  this->_findMode = true;
  ShowWindow(this->_panelFind, TRUE);
  ::SetFocus(this->_panelFind);
  int length = Edit_GetTextLength(this->_panelFind);
  Edit_SetSel(this->_panelFind, 0, length);

}

void CPanel::ExitFindMode()
{
  this->_findMode = false;
  ShowWindow(this->_panelFind, FALSE);
  SetFocusToList();
}

void CPanel::OnPanelFindEditChange()
{
  if (!this->_findMode)
  {
    return;
  }
  this->_debounceOnPanelFindEditChange(this);
}

void CPanel::OnPanelFindEditChangeDebouncedHandler()
{
  int textLen = Edit_GetTextLength(this->_panelFind);
  UString text;
  Edit_GetText(this->_panelFind, text.GetBuf(textLen), textLen);
  FindNextItem(text);
}

void CPanel::FindNextItem(UString const& text)
{
  // MessageBox(*this, (LPCWSTR)text.GetBuf(),  NULL, MB_OK);
  Z7DbgPrintW((LPCWSTR)text.GetBuf());
}