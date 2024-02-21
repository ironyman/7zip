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
    case WM_KEYDOWN:
    {
      bool alt = NWindows::IsKeyDown(VK_MENU);
      bool ctrl = NWindows::IsKeyDown(VK_CONTROL);
      bool shift = NWindows::IsKeyDown(VK_SHIFT);
      switch (wParam)
      {
        case VK_RETURN:
          if (ctrl && !alt && !shift)
          {
            if (_panel->_listView.GetSelectedCount() == 1)
            {
              _panel->ExitFindMode();
              _panel->OnNotifyActivateItems();
            }
            return 0;
          }
      }
      break;
    }
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
        {
          int textLen = Edit_GetTextLength(*this) + 1;
          UString text;
          Edit_GetText(*this, text.GetBuf_SetEnd(textLen), textLen);
          _panel->FindNextItem(text, 1);
          return 0;
        }
        case '\n':
          // Prevent MessageBeep from ctrl enter.
          return 0;
      }
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

  // Trigger resize.
  RECT rect;
  GetWindowRect(&rect);
  ChangeWindowSize(RECT_SIZE_X(rect), RECT_SIZE_Y(rect));
}

void CPanel::ExitFindMode()
{
  this->_findMode = false;
  ShowWindow(this->_panelFind, FALSE);
  SetFocusToList();

  // Trigger resize.
  RECT rect;
  GetWindowRect(&rect);
  ChangeWindowSize(RECT_SIZE_X(rect), RECT_SIZE_Y(rect));
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
  int textLen = Edit_GetTextLength(this->_panelFind) + 1;
  UString text;
  Edit_GetText(this->_panelFind, text.GetBuf_SetEnd(textLen), textLen);
  FindNextItem(text, 0);
}

void CPanel::FindNextItem(UString const& query, int skip)
{
  // MessageBox(*this, (LPCWSTR)query.GetBuf(),  NULL, MB_OK);
  Z7DbgPrintEx(L"Find", L"FindNextItem entry query %s  skip %d\n", (LPCWSTR)query.GetBuf(), skip);

  UString lowerCaseQuery(query);
  lowerCaseQuery.MakeLower_Ascii();

  // The order is wrong, we want to search not by realIndex, with which _selectedStatusVector is indexed by
  // But by the visible sorted index.
  // for (unsigned i = 0; i < _selectedStatusVector.Size(); i++)
  // {
  //   if (_selectedStatusVector[i])
  //   {
  //     searchStart = i;
  //     SelectAll(false);
  //     break;
  //   }
  // }

  // UString itemName;
  // int found = -1;
  // for (unsigned i = searchStart; i < _selectedStatusVector.Size(); i++)
  // {
  //   GetItemName(i, itemName);
  //   itemName.MakeLower_Ascii();
  //   if (itemName.Find(lowerCaseQuery) != -1)
  //   {
  //     found = i;
  //     break;
  //   }
  // }

  // if (found == -1)
  // {
  //   return;
  // }

  // _selectedStatusVector[found] = true;
  int numItems = _listView.GetItemCount();
  UString itemName;
  int searchStart = -1;

  for (int i = 0; i < numItems; i++)
  {
    const unsigned realIndex = GetRealItemIndex(i);
    if (realIndex == kParentIndex)
      continue;

    if (_selectedStatusVector[realIndex])
    {
      Z7DbgPrintEx(L"Find", L"FindNextItem found selected at %d\n", i);

      searchStart = (i + skip) % numItems;
      break;
    }
  }

  int found = -1;
  if (searchStart == -1)
  {
    searchStart = 0;
  }

  GetItemName(GetRealItemIndex(searchStart), itemName);
  itemName.MakeLower_Ascii();
  if (itemName.Find(lowerCaseQuery) != -1)
  {
    found = searchStart;
  }

  if (found != searchStart || skip != 0)
  {
    SelectAll(false);
  }

  Z7DbgPrintEx(L"Find", L"FindNextItem setting searchStart to %d %s found %d\n", searchStart, (LPCWSTR)itemName.GetBuf(), found);

  if (found == -1)
  {
    for (int i = (searchStart + 1) % numItems; i != searchStart; i = (i + 1) % numItems) {
      const unsigned realIndex = GetRealItemIndex(i);
      if (realIndex == kParentIndex)
        continue;

      GetItemName(realIndex, itemName);
      itemName.MakeLower_Ascii();
      Z7DbgPrintEx(L"Find", L"FindNextItem checking %d %s\n", i, itemName.GetBuf());
      if (itemName.Find(lowerCaseQuery) != -1)
      {
        found = i;
        break;
      }
    }
  }

  Z7DbgPrintEx(L"Find", L"FindNextItem found %d\n", found);

  if (found != -1)
  {
    _listView.SetItemState_Selected(found);
    _listView.EnsureVisible(found, false);
    _listView.SetItemState_FocusedSelected(found);
  }

  if (found != searchStart)
  {
    _listView.RedrawAllItems();
  }
}