// Theme.cpp

#include "StdAfx.h"

#include <uxtheme.h>
#include <vssym32.h>

#include "../../../Common/MyString.h"

#include "RegistryUtils.h"
#include "Theme.h"

/*
Dark mode for Win32 controls uses some undocumented uxtheme.dll functions
(exported by ordinal since Windows 10 1809). These functions are loaded dynamically,
so the program still works in older Windows versions (with partial theming).

Menu bar of the main window is drawn with undocumented WM_UAHDRAWMENU / WM_UAHDRAWMENUITEM
messages, because there is no other way to change menu bar colors.
*/

namespace NTheme {

// ---------- undocumented / dynamically loaded API ----------

enum
{
  kAppMode_Default,
  kAppMode_AllowDark,
  kAppMode_ForceDark,
  kAppMode_ForceLight
};

typedef bool (WINAPI *Func_AllowDarkModeForWindow)(HWND hwnd, bool allow);
typedef int  (WINAPI *Func_SetPreferredAppMode)(int appMode);
typedef bool (WINAPI *Func_AllowDarkModeForApp)(bool allow);
typedef void (WINAPI *Func_FlushMenuThemes)();
typedef void (WINAPI *Func_RefreshImmersiveColorPolicyState)();
typedef HRESULT (WINAPI *Func_DwmSetWindowAttribute)(HWND hwnd, DWORD attr, LPCVOID data, DWORD size);
typedef void (WINAPI *Func_RtlGetNtVersionNumbers)(LPDWORD major, LPDWORD minor, LPDWORD build);

static DWORD g_BuildNumber;
static Func_AllowDarkModeForWindow g_AllowDarkModeForWindow;
static Func_SetPreferredAppMode g_SetPreferredAppMode;
static Func_AllowDarkModeForApp g_AllowDarkModeForApp;
static Func_FlushMenuThemes g_FlushMenuThemes;
static Func_RefreshImmersiveColorPolicyState g_RefreshImmersiveColorPolicyState;
static Func_DwmSetWindowAttribute g_DwmSetWindowAttribute;

static const DWORD kBuild_DarkMode    = 17763; // Windows 10 1809
static const DWORD kBuild_AppMode     = 18362; // Windows 10 1903 : SetPreferredAppMode()
static const DWORD kBuild_DarkAttr20  = 18985;
static const DWORD kBuild_Win11       = 22000;
static const DWORD kBuild_Backdrop    = 22621; // Windows 11 22H2

static const DWORD k_DWMWA_USE_IMMERSIVE_DARK_MODE_old = 19;
static const DWORD k_DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
static const DWORD k_DWMWA_WINDOW_CORNER_PREFERENCE = 33;
static const DWORD k_DWMWA_SYSTEMBACKDROP_TYPE = 38;
static const DWORD k_DWMWCP_DEFAULT = 0;
static const DWORD k_DWMWCP_ROUND = 2;
static const DWORD k_DWMSBT_NONE = 1;
static const DWORD k_DWMSBT_MAINWINDOW = 2; // Mica

static void LoadApi()
{
  {
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll)
    {
      const Func_RtlGetNtVersionNumbers f = Z7_GET_PROC_ADDRESS(
          Func_RtlGetNtVersionNumbers, ntdll, "RtlGetNtVersionNumbers");
      if (f)
      {
        DWORD major = 0, minor = 0, build = 0;
        f(&major, &minor, &build);
        build &= ~0xF0000000;
        if (major >= 10)
          g_BuildNumber = build;
      }
    }
  }
  {
    const HMODULE dwm = ::LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dwm)
      g_DwmSetWindowAttribute = Z7_GET_PROC_ADDRESS(
          Func_DwmSetWindowAttribute, dwm, "DwmSetWindowAttribute");
  }
  if (g_BuildNumber < kBuild_DarkMode)
    return;
  const HMODULE ux = ::LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!ux)
    return;
  g_RefreshImmersiveColorPolicyState = Z7_GET_PROC_ADDRESS(
      Func_RefreshImmersiveColorPolicyState, ux, MAKEINTRESOURCEA(104));
  g_AllowDarkModeForWindow = Z7_GET_PROC_ADDRESS(
      Func_AllowDarkModeForWindow, ux, MAKEINTRESOURCEA(133));
  if (g_BuildNumber >= kBuild_AppMode)
    g_SetPreferredAppMode = Z7_GET_PROC_ADDRESS(
        Func_SetPreferredAppMode, ux, MAKEINTRESOURCEA(135));
  else
    g_AllowDarkModeForApp = Z7_GET_PROC_ADDRESS(
        Func_AllowDarkModeForApp, ux, MAKEINTRESOURCEA(135));
  g_FlushMenuThemes = Z7_GET_PROC_ADDRESS(
      Func_FlushMenuThemes, ux, MAKEINTRESOURCEA(136));
}

static void AllowDark(HWND hwnd, bool allow)
{
  if (hwnd && g_AllowDarkModeForWindow)
    g_AllowDarkModeForWindow(hwnd, allow);
}

static void DwmSetAttr(HWND hwnd, DWORD attr, DWORD value)
{
  if (g_DwmSetWindowAttribute)
    g_DwmSetWindowAttribute(hwnd, attr, &value, sizeof(value));
}

static void SetDarkTitleBar(HWND hwnd, bool dark)
{
  if (g_BuildNumber >= kBuild_DarkMode)
    DwmSetAttr(hwnd, g_BuildNumber >= kBuild_DarkAttr20 ?
        k_DWMWA_USE_IMMERSIVE_DARK_MODE :
        k_DWMWA_USE_IMMERSIVE_DARK_MODE_old, dark ? 1 : 0);
}

// ---------- state ----------

static EMode g_Mode = kMode_Light;
static bool g_Active;
static bool g_Dark;
static bool g_Fluent;
static CPalette g_Pal;
static int g_Dpi = 96;

static HBRUSH g_brWindow;
static HBRUSH g_brSurface;
static HBRUSH g_brControl;
static HFONT g_FluentFont;
static HFONT g_MenuFont;
static HIMAGELIST g_RowImageList;

static const int kFluentRowHeight = 22; // list view adds some padding to that height

EMode GetMode() { return g_Mode; }
bool IsActive() { return g_Active; }
bool IsDark() { return g_Dark; }
bool IsFluent() { return g_Fluent; }
const CPalette &Pal() { return g_Pal; }
int Scale(int px) { return ::MulDiv(px, g_Dpi, 96); }
bool NeedListCustomDraw() { return g_Fluent; }

static bool ReadRegDword(LPCWSTR keyPath, LPCWSTR name, DWORD &value)
{
  HKEY key;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
    return false;
  DWORD type = 0;
  DWORD size = sizeof(value);
  const LONG res = ::RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size);
  ::RegCloseKey(key);
  return res == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(value);
}

static bool SystemUsesDarkMode()
{
  DWORD v = 1;
  if (!ReadRegDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", v))
    return false;
  return v == 0;
}

static bool IsHighContrast()
{
  HIGHCONTRASTW hc;
  hc.cbSize = sizeof(hc);
  if (!::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0))
    return false;
  return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

// AccentPalette contains 8 RGBA colors: Light3, Light2, Light1, Base, Dark1, Dark2, Dark3, (unused)
static bool ReadAccentPalette(COLORREF *colors)
{
  HKEY key;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
      0, KEY_READ, &key) != ERROR_SUCCESS)
    return false;
  BYTE data[32];
  DWORD type = 0;
  DWORD size = sizeof(data);
  const LONG res = ::RegQueryValueExW(key, L"AccentPalette", NULL, &type, data, &size);
  ::RegCloseKey(key);
  if (res != ERROR_SUCCESS || type != REG_BINARY || size != sizeof(data))
    return false;
  for (unsigned i = 0; i < 8; i++)
    colors[i] = RGB(data[i * 4], data[i * 4 + 1], data[i * 4 + 2]);
  return true;
}

// (fg) over (bg) with alpha (a) : [0 ... 255]
static COLORREF Mix(COLORREF fg, COLORREF bg, unsigned a)
{
  return RGB(
      (GetRValue(fg) * a + GetRValue(bg) * (255 - a)) / 255,
      (GetGValue(fg) * a + GetGValue(bg) * (255 - a)) / 255,
      (GetBValue(fg) * a + GetBValue(bg) * (255 - a)) / 255);
}

static void BuildPalette(CPalette &p, bool dark, bool fluent)
{
  COLORREF accents[8];
  const bool accentDefined = ReadAccentPalette(accents);
  const COLORREF kWhite = RGB(0xFF, 0xFF, 0xFF);
  const COLORREF kBlack = RGB(0, 0, 0);
  const COLORREF kRed = RGB(0xE8, 0x11, 0x23);

  if (dark)
  {
    p.WindowBg  = RGB(0x20, 0x20, 0x20);
    p.SurfaceBg = fluent ? RGB(0x1C, 0x1C, 0x1C) : RGB(0x19, 0x19, 0x19);
    p.ControlBg = RGB(0x2D, 0x2D, 0x2D);
    p.Text          = kWhite;
    p.TextSecondary = RGB(0xC5, 0xC5, 0xC5);
    p.TextDisabled  = RGB(0x78, 0x78, 0x78);
    p.Stroke        = RGB(0x3A, 0x3A, 0x3A);
    p.Accent     = accentDefined ? accents[1] : RGB(0x60, 0xCD, 0xFF);
    p.AccentText = kBlack;
    p.Hover    = Mix(kWhite, p.SurfaceBg, 0x12);
    p.Pressed  = Mix(kWhite, p.WindowBg, 0x1C);
    p.Selected         = Mix(p.Accent, p.SurfaceBg, 0x38);
    p.SelectedHot      = Mix(p.Accent, p.SurfaceBg, 0x48);
    p.SelectedInactive = Mix(kWhite, p.SurfaceBg, 0x1C);
    p.Marked           = Mix(kRed, p.SurfaceBg, 0x50);
  }
  else
  {
    p.WindowBg  = RGB(0xF3, 0xF3, 0xF3);
    p.SurfaceBg = kWhite;
    p.ControlBg = RGB(0xFB, 0xFB, 0xFB);
    p.Text          = RGB(0x1A, 0x1A, 0x1A);
    p.TextSecondary = RGB(0x5D, 0x5D, 0x5D);
    p.TextDisabled  = RGB(0xA0, 0xA0, 0xA0);
    p.Stroke        = RGB(0xE5, 0xE5, 0xE5);
    p.Accent     = accentDefined ? accents[4] : RGB(0x00, 0x5F, 0xB8);
    p.AccentText = kWhite;
    p.Hover    = Mix(kBlack, p.SurfaceBg, 0x0A);
    p.Pressed  = Mix(kBlack, p.WindowBg, 0x12);
    p.Selected         = Mix(p.Accent, p.SurfaceBg, 0x26);
    p.SelectedHot      = Mix(p.Accent, p.SurfaceBg, 0x34);
    p.SelectedInactive = Mix(kBlack, p.SurfaceBg, 0x12);
    p.Marked           = fluent ? Mix(kRed, p.SurfaceBg, 0x30) : RGB(255, 192, 192);
  }
}

static void ReplaceBrush(HBRUSH &brush, COLORREF color)
{
  if (brush)
    ::DeleteObject(brush);
  brush = ::CreateSolidBrush(color);
}

static int CALLBACK EnumFontProc(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM lParam)
{
  *(bool *)lParam = true;
  return 0;
}

static bool FontExists(const wchar_t *name)
{
  bool found = false;
  const HDC dc = ::GetDC(NULL);
  if (dc)
  {
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, LF_FACESIZE, name, _TRUNCATE);
    ::EnumFontFamiliesExW(dc, &lf, EnumFontProc, (LPARAM)&found, 0);
    ::ReleaseDC(NULL, dc);
  }
  return found;
}

static void CreateFonts()
{
  NONCLIENTMETRICSW ncm;
  ncm.cbSize = sizeof(ncm);
  if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    return;
  g_MenuFont = ::CreateFontIndirectW(&ncm.lfMenuFont);
  LOGFONTW lf = ncm.lfMessageFont;
  const wchar_t * const kFluentFace = L"Segoe UI Variable Text";
  if (FontExists(kFluentFace))
    wcsncpy_s(lf.lfFaceName, LF_FACESIZE, kFluentFace, _TRUNCATE);
  lf.lfQuality = CLEARTYPE_QUALITY;
  g_FluentFont = ::CreateFontIndirectW(&lf);
}

static void UpdateAppMode()
{
  if (g_SetPreferredAppMode)
    g_SetPreferredAppMode(g_Dark ? kAppMode_ForceDark :
        (g_Active ? kAppMode_ForceLight : kAppMode_Default));
  else if (g_AllowDarkModeForApp)
    g_AllowDarkModeForApp(g_Dark);
  if (g_RefreshImmersiveColorPolicyState)
    g_RefreshImmersiveColorPolicyState();
  if (g_FlushMenuThemes)
    g_FlushMenuThemes();
}

// returns true, if effective look was changed
static bool Update(bool force)
{
  bool dark = false;
  bool fluent = (g_Mode >= kMode_FluentLight);
  if (g_Mode == kMode_Dark || g_Mode == kMode_FluentDark)
    dark = true;
  else if (g_Mode == kMode_Auto || g_Mode == kMode_FluentAuto)
    dark = SystemUsesDarkMode();
  if (IsHighContrast())
  {
    dark = false;
    fluent = false;
  }
  const bool active = dark || fluent;

  CPalette pal;
  BuildPalette(pal, dark, fluent);

  const bool changed = force
      || active != g_Active
      || dark != g_Dark
      || fluent != g_Fluent
      || memcmp(&pal, &g_Pal, sizeof(pal)) != 0;
  if (!changed)
    return false;

  g_Active = active;
  g_Dark = dark;
  g_Fluent = fluent;
  g_Pal = pal;
  ReplaceBrush(g_brWindow, g_Pal.WindowBg);
  ReplaceBrush(g_brSurface, g_Pal.SurfaceBg);
  ReplaceBrush(g_brControl, g_Pal.ControlBg);
  UpdateAppMode();
  return true;
}

void Init()
{
  LoadApi();
  {
    const HDC dc = ::GetDC(NULL);
    if (dc)
    {
      g_Dpi = ::GetDeviceCaps(dc, LOGPIXELSY);
      ::ReleaseDC(NULL, dc);
    }
    if (g_Dpi <= 0)
      g_Dpi = 96;
  }
  UInt32 mode = ReadThemeMode();
  if (mode >= kNumModes)
    mode = kMode_Light;
  g_Mode = (EMode)mode;
  CreateFonts();
  Update(true);
}

void SetMode(EMode mode)
{
  if ((unsigned)mode >= kNumModes)
    return;
  g_Mode = mode;
  SaveThemeMode((UInt32)mode);
  Update(true);
}

bool OnSettingChange(LPARAM lParam)
{
  if (lParam && wcscmp((LPCWSTR)lParam, L"ImmersiveColorSet") == 0)
    if (g_RefreshImmersiveColorPolicyState)
      g_RefreshImmersiveColorPolicyState();
  return Update(false);
}

bool OnColorizationChange()
{
  return Update(false);
}

COLORREF GetMarkedColor()
{
  return g_Active ? g_Pal.Marked : RGB(255, 192, 192);
}

// ---------- drawing helpers ----------

static void FillSolid(HDC hdc, const RECT &rc, COLORREF color)
{
  const COLORREF old = ::SetBkColor(hdc, color);
  ::ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
  ::SetBkColor(hdc, old);
}

static void FillRoundRect(HDC hdc, const RECT &rc, int radius, COLORREF color)
{
  const HBRUSH brush = ::CreateSolidBrush(color);
  const HGDIOBJ oldBrush = ::SelectObject(hdc, brush);
  const HGDIOBJ oldPen = ::SelectObject(hdc, ::GetStockObject(NULL_PEN));
  // with NULL_PEN RoundRect() doesn't fill right and bottom edges
  ::RoundRect(hdc, rc.left, rc.top, rc.right + 1, rc.bottom + 1, radius * 2, radius * 2);
  ::SelectObject(hdc, oldPen);
  ::SelectObject(hdc, oldBrush);
  ::DeleteObject(brush);
}

static void FrameRoundRect(HDC hdc, const RECT &rc, int radius, COLORREF color)
{
  const HPEN pen = ::CreatePen(PS_SOLID, 1, color);
  const HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
  const HGDIOBJ oldPen = ::SelectObject(hdc, pen);
  ::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
  ::SelectObject(hdc, oldPen);
  ::SelectObject(hdc, oldBrush);
  ::DeleteObject(pen);
}

static void DrawLine(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color)
{
  const HPEN pen = ::CreatePen(PS_SOLID, 1, color);
  const HGDIOBJ oldPen = ::SelectObject(hdc, pen);
  ::MoveToEx(hdc, x1, y1, NULL);
  ::LineTo(hdc, x2, y2);
  ::SelectObject(hdc, oldPen);
  ::DeleteObject(pen);
}

// double buffered painting for WM_PAINT
class CBufferedPaint
{
  HWND _hwnd;
  PAINTSTRUCT _ps;
  HDC _memDC;
  HBITMAP _bitmap;
  HGDIOBJ _oldBitmap;
  RECT _rc;
public:
  CBufferedPaint(HWND hwnd): _hwnd(hwnd), _memDC(NULL), _bitmap(NULL), _oldBitmap(NULL)
  {
    ::BeginPaint(hwnd, &_ps);
    ::GetClientRect(hwnd, &_rc);
    _memDC = ::CreateCompatibleDC(_ps.hdc);
    if (_memDC)
    {
      _bitmap = ::CreateCompatibleBitmap(_ps.hdc, _rc.right, _rc.bottom);
      if (_bitmap)
        _oldBitmap = ::SelectObject(_memDC, _bitmap);
    }
  }
  HDC DC() const { return _bitmap ? _memDC : _ps.hdc; }
  const RECT &Rect() const { return _rc; }
  ~CBufferedPaint()
  {
    if (_bitmap)
    {
      ::BitBlt(_ps.hdc, 0, 0, _rc.right, _rc.bottom, _memDC, 0, 0, SRCCOPY);
      ::SelectObject(_memDC, _oldBitmap);
      ::DeleteObject(_bitmap);
    }
    if (_memDC)
      ::DeleteDC(_memDC);
    ::EndPaint(_hwnd, &_ps);
  }
};

static bool IsClass(HWND hwnd, const wchar_t *name)
{
  wchar_t s[64];
  if (::GetClassNameW(hwnd, s, Z7_ARRAY_SIZE(s)) == 0)
    return false;
  return _wcsicmp(s, name) == 0;
}

static void GetText(HWND hwnd, UString &s)
{
  const int len = ::GetWindowTextLengthW(hwnd);
  if (len <= 0)
  {
    s.Empty();
    return;
  }
  ::GetWindowTextW(hwnd, s.GetBuf((unsigned)len), len + 1);
  s.ReleaseBuf_CalcLen((unsigned)len);
}

// ---------- window state that must be restored for default look ----------

static const wchar_t * const kProp_State = L"7zFM.Theme.State";
static const wchar_t * const kProp_Font  = L"7zFM.Theme.Font";

enum
{
  kState_RemovedClientEdge = 1 << 0,
  kState_RemovedBorder     = 1 << 1,
  kState_DoubleBuffer      = 1 << 2,
  kState_Font              = 1 << 3,
  kState_Themed            = 1 << 4,
  kState_AddedNoDivider    = 1 << 5,
  kState_Backdrop          = 1 << 6
};

static UINT_PTR GetState(HWND hwnd) { return (UINT_PTR)::GetPropW(hwnd, kProp_State); }

static void SetState(HWND hwnd, UINT_PTR state)
{
  if (state)
    ::SetPropW(hwnd, kProp_State, (HANDLE)state);
  else
    ::RemovePropW(hwnd, kProp_State);
}

static void SetStateFlag(HWND hwnd, UINT_PTR flag, bool set)
{
  const UINT_PTR state = GetState(hwnd);
  SetState(hwnd, set ? (state | flag) : (state & ~flag));
}

static void UpdateFrame(HWND hwnd)
{
  ::SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// removes (or restores) style bits
static bool RemoveStyleBits(HWND hwnd, int index, LONG_PTR bits, UINT_PTR flag, bool remove)
{
  const LONG_PTR style = ::GetWindowLongPtrW(hwnd, index);
  if (remove)
  {
    if ((style & bits) == 0)
      return false;
    ::SetWindowLongPtrW(hwnd, index, style & ~bits);
    SetStateFlag(hwnd, flag, true);
  }
  else
  {
    if ((GetState(hwnd) & flag) == 0)
      return false;
    ::SetWindowLongPtrW(hwnd, index, style | bits);
    SetStateFlag(hwnd, flag, false);
  }
  UpdateFrame(hwnd);
  return true;
}

// adds (or removes added) style bits
static void AddStyleBits(HWND hwnd, int index, LONG_PTR bits, UINT_PTR flag, bool add)
{
  const LONG_PTR style = ::GetWindowLongPtrW(hwnd, index);
  if (add)
  {
    if ((style & bits) == bits)
      return;
    ::SetWindowLongPtrW(hwnd, index, style | bits);
    SetStateFlag(hwnd, flag, true);
  }
  else
  {
    if ((GetState(hwnd) & flag) == 0)
      return;
    ::SetWindowLongPtrW(hwnd, index, style & ~bits);
    SetStateFlag(hwnd, flag, false);
  }
  UpdateFrame(hwnd);
}

// controls without WM_SETFONT return NULL for WM_GETFONT, but they don't use
// SYSTEM_FONT after WM_SETFONT(NULL). So we restore the font that control uses by default.
static HFONT GetDefaultFont(HWND hwnd)
{
  static HFONT s_IconTitleFont;
  static HFONT s_StatusFont;
  if (IsClass(hwnd, WC_LISTVIEWW))
  {
    if (!s_IconTitleFont)
    {
      LOGFONTW lf;
      if (::SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0))
        s_IconTitleFont = ::CreateFontIndirectW(&lf);
    }
    return s_IconTitleFont;
  }
  if (IsClass(hwnd, STATUSCLASSNAMEW))
  {
    if (!s_StatusFont)
    {
      NONCLIENTMETRICSW ncm;
      ncm.cbSize = sizeof(ncm);
      if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        s_StatusFont = ::CreateFontIndirectW(&ncm.lfStatusFont);
    }
    return s_StatusFont;
  }
  return (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
}

static void SetFluentFont(HWND hwnd, bool enable)
{
  const UINT_PTR state = GetState(hwnd);
  if (enable && g_FluentFont)
  {
    if ((state & kState_Font) == 0)
    {
      if (!::GetPropW(hwnd, kProp_Font))
      {
        /* some controls (list view, status bar) delete their own default font
           after WM_SETFONT. So we store a copy of original font.
           The copy is kept for next theme changes. */
        HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
        if (!font)
          font = GetDefaultFont(hwnd);
        LOGFONTW lf;
        if (font && ::GetObjectW(font, sizeof(lf), &lf) == sizeof(lf))
          ::SetPropW(hwnd, kProp_Font, (HANDLE)::CreateFontIndirectW(&lf));
      }
      SetState(hwnd, state | kState_Font);
    }
    ::SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_FluentFont, TRUE);
  }
  else if (state & kState_Font)
  {
    SetState(hwnd, state & ~kState_Font);
    ::SendMessageW(hwnd, WM_SETFONT, (WPARAM)::GetPropW(hwnd, kProp_Font), TRUE);
  }
}

static void SetTheme(HWND hwnd, const wchar_t *darkName, const wchar_t *lightName)
{
  if (!hwnd)
    return;
  AllowDark(hwnd, g_Dark);
  const wchar_t *name = g_Dark ? darkName : (g_Active ? lightName : NULL);
  ::SetWindowTheme(hwnd, name, NULL);
}

static HIMAGELIST GetRowImageList()
{
  if (!g_RowImageList)
    g_RowImageList = ImageList_Create(1, Scale(kFluentRowHeight), ILC_COLOR32, 1, 0);
  return g_RowImageList;
}

// ---------- main window ----------

#define k_WM_UAHDRAWMENU         0x0091
#define k_WM_UAHDRAWMENUITEM     0x0092

struct CUahMenu
{
  HMENU hmenu;
  HDC hdc;
  DWORD dwFlags;
};

union CUahMenuItemMetrics
{
  struct { DWORD cx; DWORD cy; } rgsizeBar[2];
  struct { DWORD cx; DWORD cy; } rgsizePopup[4];
};

struct CUahMenuPopupMetrics
{
  DWORD rgcx[4];
  DWORD fUpdateMaxWidths : 2;
};

struct CUahMenuItem
{
  int iPosition;
  CUahMenuItemMetrics umim;
  CUahMenuPopupMetrics umpm;
};

struct CUahDrawMenuItem
{
  DRAWITEMSTRUCT dis;
  CUahMenu um;
  CUahMenuItem umi;
};

static void DrawMenuBarItem(const CUahDrawMenuItem &d)
{
  wchar_t text[256];
  text[0] = 0;
  MENUITEMINFOW mii;
  memset(&mii, 0, sizeof(mii));
  mii.cbSize = sizeof(mii);
  mii.fMask = MIIM_STRING;
  mii.dwTypeData = text;
  mii.cch = Z7_ARRAY_SIZE(text) - 1;
  ::GetMenuItemInfoW(d.um.hmenu, (UINT)d.umi.iPosition, TRUE, &mii);

  const UINT st = d.dis.itemState;
  COLORREF bg = g_Pal.WindowBg;
  COLORREF fg = g_Pal.Text;
  bool fill = false;
  if (st & (ODS_HOTLIGHT | ODS_SELECTED))
  {
    bg = (st & ODS_SELECTED) ? g_Pal.Pressed : g_Pal.Hover;
    fill = true;
  }
  if (st & (ODS_GRAYED | ODS_DISABLED))
    fg = g_Pal.TextDisabled;
  else if (st & ODS_INACTIVE)
    fg = g_Pal.TextSecondary;

  const HDC hdc = d.um.hdc;
  RECT rc = d.dis.rcItem;
  FillSolid(hdc, rc, g_Pal.WindowBg);
  if (fill)
  {
    if (g_Fluent)
    {
      RECT r = rc;
      ::InflateRect(&r, 0, -Scale(2));
      FillRoundRect(hdc, r, Scale(4), Mix(g_Pal.Text, g_Pal.WindowBg, 0x10));
    }
    else
      FillSolid(hdc, rc, bg);
  }

  UINT dt = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
  if (st & ODS_NOACCEL)
    dt |= DT_HIDEPREFIX;
  const HGDIOBJ oldFont = g_MenuFont ? ::SelectObject(hdc, g_MenuFont) : NULL;
  ::SetBkMode(hdc, TRANSPARENT);
  ::SetTextColor(hdc, fg);
  ::DrawTextW(hdc, text, -1, &rc, dt);
  if (oldFont)
    ::SelectObject(hdc, oldFont);
}

// the system draws light line between menu bar and client area. We draw over it.
static void DrawMenuBarBottomLine(HWND hwnd)
{
  if (!::GetMenu(hwnd))
    return;
  RECT rcClient;
  ::GetClientRect(hwnd, &rcClient);
  ::MapWindowPoints(hwnd, NULL, (POINT *)(void *)&rcClient, 2);
  RECT rcWindow;
  ::GetWindowRect(hwnd, &rcWindow);
  ::OffsetRect(&rcClient, -rcWindow.left, -rcWindow.top);
  RECT line = rcClient;
  line.bottom = line.top;
  line.top--;
  const HDC hdc = ::GetWindowDC(hwnd);
  if (hdc)
  {
    FillSolid(hdc, line, g_Pal.WindowBg);
    ::ReleaseDC(hwnd, hdc);
  }
}

bool HandleMainWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result)
{
  if (!g_Active)
    return false;
  switch (message)
  {
    case k_WM_UAHDRAWMENU:
    {
      const CUahMenu *um = (const CUahMenu *)lParam;
      MENUBARINFO mbi;
      memset(&mbi, 0, sizeof(mbi));
      mbi.cbSize = sizeof(mbi);
      if (!::GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
        return false;
      RECT rcWindow;
      ::GetWindowRect(hwnd, &rcWindow);
      RECT rc = mbi.rcBar;
      ::OffsetRect(&rc, -rcWindow.left, -rcWindow.top);
      FillSolid(um->hdc, rc, g_Pal.WindowBg);
      result = 0;
      return true;
    }
    case k_WM_UAHDRAWMENUITEM:
      DrawMenuBarItem(*(const CUahDrawMenuItem *)lParam);
      result = 0;
      return true;
    case WM_NCPAINT:
    case WM_NCACTIVATE:
      result = ::DefWindowProcW(hwnd, message, wParam, lParam);
      DrawMenuBarBottomLine(hwnd);
      return true;
    case WM_ERASEBKGND:
      result = EraseBackground(hwnd, (HDC)wParam) ? 1 : 0;
      return true;
  }
  return false;
}

bool EraseBackground(HWND hwnd, HDC hdc)
{
  if (!g_Active)
    return false;
  RECT rc;
  ::GetClientRect(hwnd, &rc);
  ::FillRect(hdc, &rc, g_brWindow);
  return true;
}

void ApplyToMainWindow(HWND hwnd)
{
  if (!hwnd)
    return;
  AllowDark(hwnd, g_Dark);
  SetDarkTitleBar(hwnd, g_Dark);
  if (g_BuildNumber >= kBuild_Win11)
    DwmSetAttr(hwnd, k_DWMWA_WINDOW_CORNER_PREFERENCE, g_Fluent ? k_DWMWCP_ROUND : k_DWMWCP_DEFAULT);
  if (g_BuildNumber >= kBuild_Backdrop)
  {
    // DWMSBT_AUTO doesn't remove Mica that was set before. So we use DWMSBT_NONE for that case.
    const UINT_PTR state = GetState(hwnd);
    if (g_Fluent)
    {
      DwmSetAttr(hwnd, k_DWMWA_SYSTEMBACKDROP_TYPE, k_DWMSBT_MAINWINDOW);
      SetState(hwnd, state | kState_Backdrop);
    }
    else if (state & kState_Backdrop)
    {
      DwmSetAttr(hwnd, k_DWMWA_SYSTEMBACKDROP_TYPE, k_DWMSBT_NONE);
      SetState(hwnd, state & ~kState_Backdrop);
    }
  }
  UpdateFrame(hwnd);
  if (::IsWindowVisible(hwnd))
  {
    // the caption is not repainted after DWMWA_USE_IMMERSIVE_DARK_MODE change without that
    const bool isActive = (::GetActiveWindow() == hwnd);
    ::SendMessageW(hwnd, WM_NCACTIVATE, !isActive, 0);
    ::SendMessageW(hwnd, WM_NCACTIVATE, isActive, 0);
  }
}

// ---------- tool bar ----------

static void ApplyToToolTip(HWND tt)
{
  SetTheme(tt, L"DarkMode_Explorer", NULL);
}

void ApplyToToolBar(HWND hwnd)
{
  if (!hwnd)
    return;
  ApplyToToolTip((HWND)::SendMessageW(hwnd, TB_GETTOOLTIPS, 0, 0));
  // the divider is a light etched line over the tool bar
  AddStyleBits(hwnd, GWL_STYLE, CCS_NODIVIDER, kState_AddedNoDivider, g_Active);
  ::InvalidateRect(hwnd, NULL, TRUE);
}

bool HandleToolBarCustomDraw(LPNMHDR header, LRESULT &result)
{
  if (!g_Active || header->code != (UINT)NM_CUSTOMDRAW || !IsClass(header->hwndFrom, TOOLBARCLASSNAMEW))
    return false;
  NMTBCUSTOMDRAW *cd = (NMTBCUSTOMDRAW *)(void *)header;
  switch (cd->nmcd.dwDrawStage)
  {
    case CDDS_PREPAINT:
      result = CDRF_NOTIFYITEMDRAW;
      return true;
    case CDDS_ITEMPREPAINT:
    {
      const UINT st = cd->nmcd.uItemState;
      if (st & (CDIS_SELECTED | CDIS_CHECKED | CDIS_HOT))
      {
        const COLORREF bg = (st & (CDIS_SELECTED | CDIS_CHECKED)) ? g_Pal.Pressed : g_Pal.Hover;
        RECT rc = cd->nmcd.rc;
        if (g_Fluent)
        {
          ::InflateRect(&rc, -1, -1);
          FillRoundRect(cd->nmcd.hdc, rc, Scale(4), bg);
        }
        else
          FillSolid(cd->nmcd.hdc, rc, bg);
      }
      cd->clrText = (st & CDIS_DISABLED) ? g_Pal.TextDisabled : g_Pal.Text;
      cd->clrTextHighlight = g_Pal.Text;
      cd->clrBtnFace = g_Pal.WindowBg;
      cd->clrHighlightHotTrack = g_Pal.Hover;
      result = TBCDRF_NOEDGES | TBCDRF_NOBACKGROUND | TBCDRF_USECDCOLORS | TBCDRF_NOOFFSET;
      return true;
    }
  }
  return false;
}

// ---------- rebar ----------

static LRESULT CALLBACK ReBarSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR /* refData */)
{
  if (g_Active)
  switch (message)
  {
    case WM_ERASEBKGND:
      EraseBackground(hwnd, (HDC)wParam);
      return 1;
    case WM_PRINTCLIENT:
      EraseBackground(hwnd, (HDC)wParam);
      return 0;
    case WM_PAINT:
    {
      // we don't draw band borders
      PAINTSTRUCT ps;
      const HDC hdc = ::BeginPaint(hwnd, &ps);
      ::FillRect(hdc, &ps.rcPaint, g_brWindow);
      ::EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_NOTIFY:
    {
      LRESULT res = 0;
      if (HandleToolBarCustomDraw((LPNMHDR)lParam, res))
        return res;
      break;
    }
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(hwnd, ReBarSubclassProc, id);
  return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

void ApplyToReBar(HWND hwnd)
{
  if (!hwnd)
    return;
  if (g_Active)
    ::SetWindowSubclass(hwnd, ReBarSubclassProc, 1, 0);
  RemoveStyleBits(hwnd, GWL_STYLE, WS_BORDER, kState_RemovedBorder, g_Active);
  ::RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

// ---------- status bar ----------

static void PaintStatusBar(HWND hwnd)
{
  CBufferedPaint paint(hwnd);
  const HDC hdc = paint.DC();
  const RECT &rc = paint.Rect();
  FillSolid(hdc, rc, g_Pal.WindowBg);
  {
    RECT line = rc;
    line.bottom = line.top + 1;
    FillSolid(hdc, line, g_Pal.Stroke);
  }
  const HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
  const HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
  ::SetBkMode(hdc, TRANSPARENT);
  ::SetTextColor(hdc, g_Pal.TextSecondary);

  int numParts = (int)::SendMessageW(hwnd, SB_GETPARTS, 0, 0);
  if (numParts > 64)
    numParts = 64;
  UString s;
  for (int i = 0; i < numParts; i++)
  {
    RECT pr;
    if (!::SendMessageW(hwnd, SB_GETRECT, (WPARAM)i, (LPARAM)&pr))
      continue;
    const LRESULT lenRes = ::SendMessageW(hwnd, SB_GETTEXTLENGTHW, (WPARAM)i, 0);
    if (HIWORD(lenRes) & SBT_OWNERDRAW)
      continue;
    const unsigned len = LOWORD(lenRes);
    ::SendMessageW(hwnd, SB_GETTEXTW, (WPARAM)i, (LPARAM)s.GetBuf(len));
    s.ReleaseBuf_CalcLen(len);
    if (i + 1 < numParts)
      DrawLine(hdc, pr.right, pr.top + Scale(4), pr.right, pr.bottom - Scale(3), g_Pal.Stroke);
    pr.left += Scale(8);
    pr.right -= Scale(4);
    ::DrawTextW(hdc, s, (int)s.Len(), &pr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
  if (oldFont)
    ::SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK StatusBarSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR /* refData */)
{
  if (g_Active)
  switch (message)
  {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      PaintStatusBar(hwnd);
      return 0;
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(hwnd, StatusBarSubclassProc, id);
  return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

void ApplyToStatusBar(HWND hwnd)
{
  if (!hwnd)
    return;
  if (g_Active)
    ::SetWindowSubclass(hwnd, StatusBarSubclassProc, 1, 0);
  SetFluentFont(hwnd, g_Fluent);
  ::InvalidateRect(hwnd, NULL, TRUE);
}

// ---------- edit ----------

HBRUSH HandleCtlColorEdit(HDC hdc)
{
  if (!g_Active)
    return NULL;
  ::SetTextColor(hdc, g_Pal.Text);
  ::SetBkColor(hdc, g_Pal.ControlBg);
  return g_brControl;
}

void ApplyToEdit(HWND hwnd)
{
  if (!hwnd)
    return;
  SetTheme(hwnd, L"DarkMode_CFD", NULL);
  RemoveStyleBits(hwnd, GWL_EXSTYLE, WS_EX_CLIENTEDGE, kState_RemovedClientEdge, g_Active);
  RemoveStyleBits(hwnd, GWL_STYLE, WS_BORDER, kState_RemovedBorder, g_Active);
  SetFluentFont(hwnd, g_Fluent);
  ::InvalidateRect(hwnd, NULL, TRUE);
}

// ---------- combo box ----------

static LRESULT CALLBACK ComboSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR /* refData */)
{
  if (g_Active)
  switch (message)
  {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
      return (LRESULT)HandleCtlColorEdit((HDC)wParam);
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(hwnd, ComboSubclassProc, id);
  return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

// ComboBoxEx draws items with system colors. So we draw items in dark mode.
static void DrawComboExItem(HWND comboEx, const DRAWITEMSTRUCT &dis)
{
  const HDC hdc = dis.hDC;
  const RECT &rc = dis.rcItem;
  const bool isEdit = (dis.itemState & ODS_COMBOBOXEDIT) != 0;
  const bool selected = !isEdit && (dis.itemState & ODS_SELECTED) != 0;
  FillSolid(hdc, rc, selected ? g_Pal.Selected : g_Pal.ControlBg);

  // (-1) is the item of edit field in ComboBoxEx
  int index = isEdit ? -1 : (int)dis.itemID;
  if (!isEdit && index < 0)
    return;
  wchar_t text[1024];
  text[0] = 0;
  COMBOBOXEXITEMW item;
  memset(&item, 0, sizeof(item));
  item.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_INDENT;
  item.iItem = index;
  item.pszText = text;
  item.cchTextMax = Z7_ARRAY_SIZE(text);
  if (!::SendMessageW(comboEx, CBEM_GETITEMW, 0, (LPARAM)&item))
    return;

  int x = rc.left + Scale(2);
  if (!isEdit)
    x += item.iIndent * Scale(10);
  const HIMAGELIST il = (HIMAGELIST)::SendMessageW(comboEx, CBEM_GETIMAGELIST, 0, 0);
  if (il)
  {
    int cx = 0, cy = 0;
    ImageList_GetIconSize(il, &cx, &cy);
    const int image = (dis.itemState & ODS_SELECTED) ? item.iSelectedImage : item.iImage;
    if (image >= 0)
      ImageList_Draw(il, image, hdc, x, rc.top + (rc.bottom - rc.top - cy) / 2, ILD_TRANSPARENT);
    x += cx + Scale(4);
  }
  if (isEdit)
    return; // edit control shows the text
  RECT tr = rc;
  tr.left = x;
  ::SetBkMode(hdc, TRANSPARENT);
  ::SetTextColor(hdc, g_Pal.Text);
  ::DrawTextW(hdc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK ComboExSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR /* refData */)
{
  if (g_Dark && message == WM_DRAWITEM)
  {
    const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lParam;
    if (dis->CtlType == ODT_COMBOBOX)
    {
      DrawComboExItem(hwnd, *dis);
      return TRUE;
    }
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(hwnd, ComboExSubclassProc, id);
  return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

static void ApplyToComboBox(HWND combo)
{
  if (!combo)
    return;
  SetTheme(combo, L"DarkMode_CFD", NULL);
  if (g_Active)
    ::SetWindowSubclass(combo, ComboSubclassProc, 1, 0);
  COMBOBOXINFO cbi;
  memset(&cbi, 0, sizeof(cbi));
  cbi.cbSize = sizeof(cbi);
  if (::GetComboBoxInfo(combo, &cbi))
  {
    SetTheme(cbi.hwndList, L"DarkMode_Explorer", NULL);
    SetTheme(cbi.hwndItem, L"DarkMode_CFD", NULL);
  }
  ::InvalidateRect(combo, NULL, TRUE);
}

void ApplyToComboBoxEx(HWND hwnd)
{
  if (!hwnd)
    return;
  if (g_Active)
    ::SetWindowSubclass(hwnd, ComboExSubclassProc, 1, 0);
  ApplyToComboBox((HWND)::SendMessageW(hwnd, CBEM_GETCOMBOCONTROL, 0, 0));
  ::RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

// ---------- list view ----------

static LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR /* refData */)
{
  if (g_Active)
  switch (message)
  {
    case WM_NOTIFY:
    {
      // header sends NM_CUSTOMDRAW to list view (not to parent of list view)
      const NMHDR *h = (const NMHDR *)lParam;
      if (h->code == (UINT)NM_CUSTOMDRAW && h->hwndFrom == ListView_GetHeader(hwnd))
      {
        NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lParam;
        switch (cd->dwDrawStage)
        {
          case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
          case CDDS_ITEMPREPAINT:
            ::SetTextColor(cd->hdc, g_Fluent ? g_Pal.TextSecondary : g_Pal.Text);
            return CDRF_DODEFAULT;
        }
      }
      break;
    }
    case WM_CTLCOLOREDIT:
      return (LRESULT)HandleCtlColorEdit((HDC)wParam);
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(hwnd, ListViewSubclassProc, id);
  return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

void ApplyToListView(HWND hwnd)
{
  if (!hwnd)
    return;
  const HWND header = ListView_GetHeader(hwnd);
  if (!g_Active && (GetState(hwnd) & kState_Themed) == 0)
    return;

  SetTheme(hwnd, L"DarkMode_Explorer", L"Explorer");
  SetTheme(header, L"DarkMode_ItemsView", L"ItemsView");
  ApplyToToolTip(ListView_GetToolTips(hwnd));

  if (g_Active)
  {
    ::SetWindowSubclass(hwnd, ListViewSubclassProc, 1, 0);
    ListView_SetBkColor(hwnd, g_Pal.SurfaceBg);
    ListView_SetTextBkColor(hwnd, g_Pal.SurfaceBg);
    ListView_SetTextColor(hwnd, g_Pal.Text);
    if ((ListView_GetExtendedListViewStyle(hwnd) & LVS_EX_DOUBLEBUFFER) == 0)
    {
      ListView_SetExtendedListViewStyleEx(hwnd, LVS_EX_DOUBLEBUFFER, LVS_EX_DOUBLEBUFFER);
      SetStateFlag(hwnd, kState_DoubleBuffer, true);
    }
  }
  else
  {
    ListView_SetBkColor(hwnd, ::GetSysColor(COLOR_WINDOW));
    ListView_SetTextBkColor(hwnd, ::GetSysColor(COLOR_WINDOW));
    ListView_SetTextColor(hwnd, ::GetSysColor(COLOR_WINDOWTEXT));
    if (GetState(hwnd) & kState_DoubleBuffer)
    {
      ListView_SetExtendedListViewStyleEx(hwnd, LVS_EX_DOUBLEBUFFER, 0);
      SetStateFlag(hwnd, kState_DoubleBuffer, false);
    }
  }
  RemoveStyleBits(hwnd, GWL_EXSTYLE, WS_EX_CLIENTEDGE, kState_RemovedClientEdge, g_Active);
  // the state image list changes the height of rows.
  // It must be changed before font, because font change recalculates the height of rows.
  ListView_SetImageList(hwnd, g_Fluent ? GetRowImageList() : NULL, LVSIL_STATE);
  SetFluentFont(hwnd, g_Fluent);
  // list view doesn't reduce the height of rows after state image list removing.
  // So we set small image list again to recalculate the height.
  ListView_SetImageList(hwnd, ListView_GetImageList(hwnd, LVSIL_SMALL), LVSIL_SMALL);
  SetStateFlag(hwnd, kState_Themed, g_Active);

  if (header)
    ::SendMessageW(header, WM_THEMECHANGED, 0, 0);
  ::SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
  ::RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void GetListItemText(HWND lv, int item, int subItem, wchar_t *buf, int size)
{
  buf[0] = 0;
  LVITEMW it;
  memset(&it, 0, sizeof(it));
  it.iSubItem = subItem;
  it.pszText = buf;
  it.cchTextMax = size;
  ::SendMessageW(lv, LVM_GETITEMTEXTW, (WPARAM)item, (LPARAM)&it);
}

bool DrawListItem(LPNMLVCUSTOMDRAW cd, bool marked, bool forceActiveSelection, LRESULT &result)
{
  if (!g_Fluent)
    return false;
  const HWND lv = cd->nmcd.hdr.hwndFrom;
  const HDC hdc = cd->nmcd.hdc;
  const int item = (int)cd->nmcd.dwItemSpec;

  RECT rcItem;
  if (!ListView_GetItemRect(lv, item, &rcItem, LVIR_BOUNDS))
    return false;
  RECT rcClient;
  ::GetClientRect(lv, &rcClient);

  const UINT state = ListView_GetItemState(lv, item,
      LVIS_SELECTED | LVIS_FOCUSED | LVIS_CUT | LVIS_DROPHILITED);
  const bool listFocused = forceActiveSelection || ::GetFocus() == lv;
  const bool selected = forceActiveSelection || (state & (LVIS_SELECTED | LVIS_DROPHILITED)) != 0;
  const bool hot = (ListView_GetHotItem(lv) == item);
  const DWORD view = (DWORD)::GetWindowLongPtrW(lv, GWL_STYLE) & LVS_TYPEMASK;

  FillSolid(hdc, rcItem, g_Pal.SurfaceBg);

  RECT rcSel = rcItem;
  if (view == LVS_REPORT)
  {
    if (rcSel.right > rcClient.right)
      rcSel.right = rcClient.right;
    ::InflateRect(&rcSel, -Scale(4), -Scale(1));
  }
  else
    ::InflateRect(&rcSel, -Scale(1), -Scale(1));

  bool fill = true;
  COLORREF fillColor = 0;
  if (selected)
  {
    fillColor = listFocused ? (hot ? g_Pal.SelectedHot : g_Pal.Selected) : g_Pal.SelectedInactive;
    if (marked)
      fillColor = Mix(g_Pal.Marked, fillColor, 0x80);
  }
  else if (marked)
    fillColor = g_Pal.Marked;
  else if (hot)
    fillColor = g_Pal.Hover;
  else
    fill = false;
  const int radius = Scale(4);
  if (fill)
    FillRoundRect(hdc, rcSel, radius, fillColor);

  if (selected && listFocused)
  {
    // selection indicator ("pill") like in WinUI list views
    const int h = rcSel.bottom - rcSel.top;
    int ih = h - Scale(10);
    if (ih > Scale(16))
      ih = Scale(16);
    if (ih < Scale(6))
      ih = h;
    RECT pill;
    pill.left = rcSel.left;
    pill.right = pill.left + Scale(3);
    pill.top = rcSel.top + (h - ih) / 2;
    pill.bottom = pill.top + ih;
    FillRoundRect(hdc, pill, Scale(1) + 1, g_Pal.Accent);
  }
  else if (listFocused && (state & LVIS_FOCUSED))
    FrameRoundRect(hdc, rcSel, radius, g_Pal.TextSecondary);

  const HFONT font = (HFONT)::SendMessageW(lv, WM_GETFONT, 0, 0);
  const HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
  ::SetBkMode(hdc, TRANSPARENT);
  const bool isCut = (state & LVIS_CUT) != 0;
  const COLORREF textColor = isCut ? g_Pal.TextSecondary : g_Pal.Text;

  // icon
  {
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_IMAGE | LVIF_STATE;
    it.iItem = item;
    it.stateMask = LVIS_OVERLAYMASK;
    RECT rcIcon;
    const HIMAGELIST il = ListView_GetImageList(lv, view == LVS_ICON ? LVSIL_NORMAL : LVSIL_SMALL);
    if (il
        && ::SendMessageW(lv, LVM_GETITEMW, 0, (LPARAM)&it)
        && it.iImage >= 0
        && ListView_GetItemRect(lv, item, &rcIcon, LVIR_ICON))
    {
      int cx = 0, cy = 0;
      ImageList_GetIconSize(il, &cx, &cy);
      const int x = rcIcon.left + (rcIcon.right - rcIcon.left - cx) / 2;
      const int y = rcIcon.top + (rcIcon.bottom - rcIcon.top - cy) / 2;
      UINT flags = ILD_TRANSPARENT | (it.state & LVIS_OVERLAYMASK);
      if (isCut)
        flags |= ILD_BLEND50;
      ImageList_DrawEx(il, it.iImage, hdc, x, y, 0, 0, CLR_NONE, isCut ? g_Pal.SurfaceBg : CLR_DEFAULT, flags);
    }
  }

  wchar_t text[1024];
  const UINT kTextFlags = DT_NOPREFIX | DT_END_ELLIPSIS;

  if (view == LVS_REPORT)
  {
    const int numColumns = Header_GetItemCount(ListView_GetHeader(lv));
    for (int sub = 0; sub < numColumns; sub++)
    {
      RECT rc;
      if (!ListView_GetSubItemRect(lv, item, sub, sub == 0 ? LVIR_LABEL : LVIR_BOUNDS, &rc))
        continue;
      if (rc.right <= rcClient.left || rc.left >= rcClient.right || rc.right <= rc.left)
        continue;
      LVCOLUMNW col;
      memset(&col, 0, sizeof(col));
      col.mask = LVCF_FMT;
      ::SendMessageW(lv, LVM_GETCOLUMNW, (WPARAM)sub, (LPARAM)&col);
      UINT dt = kTextFlags | DT_SINGLELINE | DT_VCENTER;
      switch (col.fmt & LVCFMT_JUSTIFYMASK)
      {
        case LVCFMT_RIGHT: dt |= DT_RIGHT; break;
        case LVCFMT_CENTER: dt |= DT_CENTER; break;
      }
      rc.left += Scale(sub == 0 ? 4 : 6);
      rc.right -= Scale(6);
      GetListItemText(lv, item, sub, text, Z7_ARRAY_SIZE(text));
      ::SetTextColor(hdc, sub == 0 ? textColor : g_Pal.TextSecondary);
      ::DrawTextW(hdc, text, -1, &rc, dt);
    }
  }
  else
  {
    RECT rc;
    if (ListView_GetItemRect(lv, item, &rc, LVIR_LABEL))
    {
      GetListItemText(lv, item, 0, text, Z7_ARRAY_SIZE(text));
      ::SetTextColor(hdc, textColor);
      if (view == LVS_ICON)
        ::DrawTextW(hdc, text, -1, &rc, kTextFlags | DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL);
      else
      {
        rc.left += Scale(2);
        ::DrawTextW(hdc, text, -1, &rc, kTextFlags | DT_SINGLELINE | DT_VCENTER);
      }
    }
  }

  if (oldFont)
    ::SelectObject(hdc, oldFont);
  result = CDRF_SKIPDEFAULT;
  return true;
}

// ---------- dialogs (dark mode only) ----------

static void DrawCheckBox(NMCUSTOMDRAW *cd, LRESULT &result)
{
  const HWND button = cd->hdr.hwndFrom;
  const LONG_PTR style = ::GetWindowLongPtrW(button, GWL_STYLE);
  const LONG_PTR type = style & BS_TYPEMASK;
  const bool isRadio = (type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON);
  const HTHEME theme = ::OpenThemeData(button, L"Button");
  if (!theme)
  {
    result = CDRF_DODEFAULT;
    return;
  }
  const HDC hdc = cd->hdc;
  const UINT st = cd->uItemState;
  const LRESULT check = ::SendMessageW(button, BM_GETCHECK, 0, 0);
  const int offset = (st & CDIS_DISABLED) ? 3 : (st & CDIS_SELECTED) ? 2 : (st & CDIS_HOT) ? 1 : 0;
  int stateId;
  if (check == BST_CHECKED)
    stateId = CBS_CHECKEDNORMAL;
  else if (check == BST_INDETERMINATE && !isRadio)
    stateId = CBS_MIXEDNORMAL;
  else
    stateId = CBS_UNCHECKEDNORMAL;
  stateId += offset;
  const int part = isRadio ? BP_RADIOBUTTON : BP_CHECKBOX;

  RECT rc;
  ::GetClientRect(button, &rc);
  FillSolid(hdc, rc, g_Pal.WindowBg);

  SIZE size = { Scale(13), Scale(13) };
  ::GetThemePartSize(theme, hdc, part, stateId, NULL, TS_DRAW, &size);
  const bool multiLine = (style & BS_MULTILINE) != 0;
  RECT box;
  box.left = rc.left;
  box.top = multiLine ? rc.top + Scale(1) : rc.top + (rc.bottom - rc.top - size.cy) / 2;
  box.right = box.left + size.cx;
  box.bottom = box.top + size.cy;
  ::DrawThemeBackground(theme, hdc, part, stateId, &box, NULL);
  ::CloseThemeData(theme);

  UString text;
  GetText(button, text);
  const LRESULT uiState = ::SendMessageW(button, WM_QUERYUISTATE, 0, 0);
  UINT dt = multiLine ? DT_WORDBREAK : (DT_SINGLELINE | DT_VCENTER);
  if (uiState & UISF_HIDEACCEL)
    dt |= DT_HIDEPREFIX;
  RECT tr = rc;
  tr.left = box.right + Scale(4);
  const HFONT font = (HFONT)::SendMessageW(button, WM_GETFONT, 0, 0);
  const HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
  ::SetBkMode(hdc, TRANSPARENT);
  ::SetTextColor(hdc, (st & CDIS_DISABLED) ? g_Pal.TextDisabled : g_Pal.Text);
  ::DrawTextW(hdc, text, (int)text.Len(), &tr, dt);
  if ((st & CDIS_FOCUS) && (uiState & UISF_HIDEFOCUS) == 0 && !text.IsEmpty())
  {
    RECT fr = tr;
    ::DrawTextW(hdc, text, (int)text.Len(), &fr, dt | DT_CALCRECT);
    if (!multiLine)
      ::OffsetRect(&fr, 0, (tr.bottom - tr.top - (fr.bottom - fr.top)) / 2);
    ::InflateRect(&fr, 1, 1);
    ::SetTextColor(hdc, g_Pal.Text);
    ::DrawFocusRect(hdc, &fr);
  }
  if (oldFont)
    ::SelectObject(hdc, oldFont);
  result = CDRF_SKIPDEFAULT;
}

static LRESULT CALLBACK GroupBoxSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR /* refData */)
{
  if (g_Dark && message == WM_PAINT)
  {
    PAINTSTRUCT ps;
    const HDC hdc = ::BeginPaint(hwnd, &ps);
    RECT rc;
    ::GetClientRect(hwnd, &rc);
    const HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
    const HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
    UString text;
    GetText(hwnd, text);
    SIZE ts = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, L"W", 1, &ts);
    RECT frame = rc;
    frame.top += ts.cy / 2;
    FrameRoundRect(hdc, frame, Scale(4), g_Pal.Stroke);
    if (!text.IsEmpty())
    {
      RECT tr = rc;
      tr.left += Scale(8);
      ::DrawTextW(hdc, text, (int)text.Len(), &tr, DT_SINGLELINE | DT_CALCRECT);
      tr.right += Scale(4);
      FillSolid(hdc, tr, g_Pal.WindowBg);
      tr.left += Scale(2);
      ::SetBkMode(hdc, TRANSPARENT);
      ::SetTextColor(hdc, ::IsWindowEnabled(hwnd) ? g_Pal.Text : g_Pal.TextDisabled);
      const LRESULT uiState = ::SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0);
      ::DrawTextW(hdc, text, (int)text.Len(), &tr,
          DT_SINGLELINE | ((uiState & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0));
    }
    if (oldFont)
      ::SelectObject(hdc, oldFont);
    ::EndPaint(hwnd, &ps);
    return 0;
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(hwnd, GroupBoxSubclassProc, id);
  return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

static void PaintTabControl(HWND hwnd)
{
  CBufferedPaint paint(hwnd);
  const HDC hdc = paint.DC();
  const RECT &rc = paint.Rect();
  FillSolid(hdc, rc, g_Pal.WindowBg);
  const HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
  const HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
  ::SetBkMode(hdc, TRANSPARENT);

  const int numItems = TabCtrl_GetItemCount(hwnd);
  const int sel = TabCtrl_GetCurSel(hwnd);
  RECT body = rc;
  TabCtrl_AdjustRect(hwnd, FALSE, &body);
  ::InflateRect(&body, Scale(4), Scale(4));
  FrameRoundRect(hdc, body, Scale(4), g_Pal.Stroke);

  POINT pt;
  ::GetCursorPos(&pt);
  ::ScreenToClient(hwnd, &pt);
  for (int i = 0; i < numItems; i++)
  {
    RECT r;
    if (!TabCtrl_GetItemRect(hwnd, i, &r))
      continue;
    wchar_t text[256];
    text[0] = 0;
    TCITEMW ti;
    memset(&ti, 0, sizeof(ti));
    ti.mask = TCIF_TEXT;
    ti.pszText = text;
    ti.cchTextMax = Z7_ARRAY_SIZE(text);
    ::SendMessageW(hwnd, TCM_GETITEMW, (WPARAM)i, (LPARAM)&ti);
    const bool isSel = (i == sel);
    if (isSel || ::PtInRect(&r, pt))
    {
      RECT f = r;
      ::InflateRect(&f, -1, -1);
      FillRoundRect(hdc, f, Scale(4), isSel ? g_Pal.ControlBg : g_Pal.Hover);
    }
    ::SetTextColor(hdc, isSel ? g_Pal.Text : g_Pal.TextSecondary);
    ::DrawTextW(hdc, text, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
  }
  if (oldFont)
    ::SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK TabSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR /* refData */)
{
  if (g_Dark)
  switch (message)
  {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      PaintTabControl(hwnd);
      return 0;
    case WM_MOUSEMOVE:
    {
      // repaint hot tab
      TRACKMOUSEEVENT tme;
      tme.cbSize = sizeof(tme);
      tme.dwFlags = TME_LEAVE;
      tme.hwndTrack = hwnd;
      tme.dwHoverTime = 0;
      ::TrackMouseEvent(&tme);
      ::InvalidateRect(hwnd, NULL, FALSE);
      break;
    }
    case WM_MOUSELEAVE:
      ::InvalidateRect(hwnd, NULL, FALSE);
      break;
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(hwnd, TabSubclassProc, id);
  return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

static LRESULT CALLBACK DialogSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR /* refData */)
{
  if (g_Dark)
  switch (message)
  {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      const HDC hdc = (HDC)wParam;
      ::SetTextColor(hdc, ::IsWindowEnabled((HWND)lParam) || message == WM_CTLCOLORDLG ?
          g_Pal.Text : g_Pal.TextDisabled);
      ::SetBkColor(hdc, g_Pal.WindowBg);
      return (LRESULT)g_brWindow;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
      return (LRESULT)HandleCtlColorEdit((HDC)wParam);
    case WM_ERASEBKGND:
      EraseBackground(hwnd, (HDC)wParam);
      return 1;
    case WM_NOTIFY:
    {
      NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lParam;
      if (cd->hdr.code == (UINT)NM_CUSTOMDRAW
          && cd->dwDrawStage == CDDS_PREPAINT
          && IsClass(cd->hdr.hwndFrom, WC_BUTTONW))
      {
        const LONG_PTR style = ::GetWindowLongPtrW(cd->hdr.hwndFrom, GWL_STYLE);
        switch (style & BS_TYPEMASK)
        {
          case BS_CHECKBOX:
          case BS_AUTOCHECKBOX:
          case BS_3STATE:
          case BS_AUTO3STATE:
          case BS_RADIOBUTTON:
          case BS_AUTORADIOBUTTON:
            if ((style & BS_PUSHLIKE) == 0)
            {
              // we are before dialog procedure, so we return result directly
              LRESULT res = CDRF_DODEFAULT;
              DrawCheckBox(cd, res);
              return res;
            }
        }
      }
      break;
    }
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(hwnd, DialogSubclassProc, id);
  return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

static void ApplyToDialogControl(HWND hwnd)
{
  wchar_t name[64];
  if (::GetClassNameW(hwnd, name, Z7_ARRAY_SIZE(name)) == 0)
    return;
  const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
  if (_wcsicmp(name, WC_BUTTONW) == 0)
  {
    if ((style & BS_TYPEMASK) == BS_GROUPBOX)
      ::SetWindowSubclass(hwnd, GroupBoxSubclassProc, 1, 0);
    else
      SetTheme(hwnd, L"DarkMode_Explorer", NULL);
  }
  else if (_wcsicmp(name, WC_EDITW) == 0)
    SetTheme(hwnd, (style & ES_MULTILINE) ? L"DarkMode_Explorer" : L"DarkMode_CFD", NULL);
  else if (_wcsicmp(name, WC_COMBOBOXW) == 0)
    ApplyToComboBox(hwnd);
  else if (_wcsicmp(name, WC_COMBOBOXEXW) == 0)
    ApplyToComboBoxEx(hwnd);
  else if (_wcsicmp(name, WC_LISTVIEWW) == 0)
    ApplyToListView(hwnd);
  else if (_wcsicmp(name, WC_TREEVIEWW) == 0)
  {
    SetTheme(hwnd, L"DarkMode_Explorer", NULL);
    TreeView_SetBkColor(hwnd, g_Pal.SurfaceBg);
    TreeView_SetTextColor(hwnd, g_Pal.Text);
  }
  else if (_wcsicmp(name, WC_LISTBOXW) == 0 || _wcsicmp(name, WC_SCROLLBARW) == 0)
    SetTheme(hwnd, L"DarkMode_Explorer", NULL);
  else if (_wcsicmp(name, WC_TABCONTROLW) == 0)
    ::SetWindowSubclass(hwnd, TabSubclassProc, 1, 0);
  else if (_wcsicmp(name, TOOLTIPS_CLASSW) == 0)
    ApplyToToolTip(hwnd);
  else if (_wcsicmp(name, L"#32770") == 0)
    OnDialogInit(hwnd);
}

static BOOL CALLBACK EnumDialogChildProc(HWND hwnd, LPARAM)
{
  ApplyToDialogControl(hwnd);
  return TRUE;
}

static const wchar_t * const kProp_Dialog = L"7zFM.Theme.Dialog";

void OnDialogInit(HWND dialog)
{
  if (!g_Dark || !dialog || ::GetPropW(dialog, kProp_Dialog))
    return;
  ::SetPropW(dialog, kProp_Dialog, (HANDLE)1);
  ::SetWindowSubclass(dialog, DialogSubclassProc, 1, 0);
  ::EnableThemeDialogTexture(dialog, ETDT_DISABLE);
  AllowDark(dialog, true);
  const LONG_PTR style = ::GetWindowLongPtrW(dialog, GWL_STYLE);
  if (style & WS_CHILD)
  {
    // property page: we also change property sheet window
    const HWND parent = ::GetParent(dialog);
    if (parent && IsClass(parent, L"#32770"))
      OnDialogInit(parent);
  }
  else
    SetDarkTitleBar(dialog, true);
  ::EnumChildWindows(dialog, EnumDialogChildProc, 0);
  ::SendMessageW(dialog, WM_THEMECHANGED, 0, 0);
}

}
