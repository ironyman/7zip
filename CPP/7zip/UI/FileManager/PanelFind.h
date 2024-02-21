// Panel.h

#ifndef ZIP7_INC_PANEL_FIND_H
#define ZIP7_INC_PANEL_FIND_H

#include "../../../Common/MyWindows.h"


#if defined(__MINGW32__) || defined(__MINGW64__)
#include <shlobj.h>
#else
#include <ShlObj.h>
#endif

#include "../../../Common/Common.h"
// #include "Panel.h"
// #include "App.h"
#include "../../../Windows/Window.h"
#include "../../../Windows/Control/Window2.h"

class CPanel;

class CPanelFind Z7_final: public NWindows::NControl::CWindow2
{
//   virtual bool OnCommand(unsigned code, unsigned itemID, LPARAM lParam, LRESULT &result) Z7_override;
//   virtual bool OnCreate(CREATESTRUCT *createStruct) Z7_override;

  WNDPROC _origWindowProc;
  void SetWindowProc();

public:
  bool Create(PCWSTR text, HINSTANCE hInstance, CPanel *panel, HWND hwndParent, UINT id);
  virtual LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam) Z7_override;
  CPanel *_panel;
};

#endif
