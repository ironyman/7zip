// Theme.h

#ifndef ZIP7_INC_FM_THEME_H
#define ZIP7_INC_FM_THEME_H

#include "../../../Common/MyWindows.h"

#include <CommCtrl.h>

/*
UI themes for File Manager:
  Light / Dark / Auto            : classic Win32 controls. Light is the default system look.
  Fluent Light / Dark / Auto     : Win32 controls restyled after Fluent (WinUI) design:
                                   Mica title bar, Segoe UI Variable, taller rows,
                                   rounded selection with accent indicator.
Auto modes follow the "app mode" (light / dark) in Windows settings.
High contrast mode always uses the default system look.
*/

namespace NTheme {

enum EMode
{
  kMode_Light = 0,
  kMode_Dark,
  kMode_Auto,
  kMode_FluentLight,
  kMode_FluentDark,
  kMode_FluentAuto,
  kNumModes
};

struct CPalette
{
  COLORREF WindowBg;      // main window, toolbars, menu bar, dialogs
  COLORREF SurfaceBg;     // list views
  COLORREF ControlBg;     // edit and combo boxes
  COLORREF Text;
  COLORREF TextSecondary;
  COLORREF TextDisabled;
  COLORREF Stroke;
  COLORREF Hover;
  COLORREF Pressed;
  COLORREF Selected;
  COLORREF SelectedHot;
  COLORREF SelectedInactive;
  COLORREF Marked;        // items marked in "alternative selection" mode
  COLORREF Accent;
  COLORREF AccentText;    // text over Accent fill
};

// Init() must be called before the main window is created.
void Init();

EMode GetMode();
// SetMode() saves mode to registry. Caller must re-apply theme to existing windows.
void SetMode(EMode mode);

// false : default system look (Light mode or high contrast). Controls are not changed.
bool IsActive();
bool IsDark();
bool IsFluent();
const CPalette &Pal();
int Scale(int px);

// these functions return true, if effective look was changed and windows must be updated
bool OnSettingChange(LPARAM lParam);
bool OnColorizationChange();

// ApplyTo*() functions can be called for any mode.
// For default look they restore the state that was changed by previous calls.
void ApplyToMainWindow(HWND hwnd);
void ApplyToListView(HWND hwnd);
void ApplyToReBar(HWND hwnd);
void ApplyToToolBar(HWND hwnd);
void ApplyToComboBoxEx(HWND hwnd);
void ApplyToStatusBar(HWND hwnd);
void ApplyToEdit(HWND hwnd);

// for main window procedure: menu bar drawing and background
bool HandleMainWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result);
bool HandleToolBarCustomDraw(LPNMHDR header, LRESULT &result);
// returns NULL, if default processing is required
HBRUSH HandleCtlColorEdit(HDC hdc);
bool EraseBackground(HWND hwnd, HDC hdc);

COLORREF GetMarkedColor();

// Fluent list view items are drawn by File Manager instead of list view control.
bool NeedListCustomDraw();
// for NM_CUSTOMDRAW / CDDS_ITEMPREPAINT
bool DrawListItem(LPNMLVCUSTOMDRAW cd, bool marked, bool forceActiveSelection, LRESULT &result);

// is called for File Manager dialogs after WM_INITDIALOG
void OnDialogInit(HWND dialog);
}

#endif
