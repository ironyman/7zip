// Windows/Clipboard.h

#ifndef ZIP7_INC_CLIPBOARD_H
#define ZIP7_INC_CLIPBOARD_H

#include "../Common/MyString.h"
#include <vector>
#include <string>

namespace NWindows {

class CClipboard
{
  bool m_Open;
public:
  CClipboard(): m_Open(false) {}
  ~CClipboard() { Close(); }
  bool Open(HWND wndNewOwner) throw();
  bool Close() throw();
};

bool ClipboardIsFormatAvailableHDROP();

// bool ClipboardGetFileNames(UStringVector &names);
// bool ClipboardGetTextString(AString &s);
bool ClipboardSetText(HWND owner, const UString &s);
void ClipboardSetFiles(HWND owner, const std::vector<std::wstring>& filePaths, DWORD effect);
// void ClipboardGetFiles(HWND owner, std::vector<std::wstring>& filePaths, DWORD& effect);
void ClipboardGetFiles(HWND owner, UStringVector& filePaths, DWORD& effect);

}

#endif
