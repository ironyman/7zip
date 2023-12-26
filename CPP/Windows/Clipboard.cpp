// Windows/Clipboard.cpp

#include "StdAfx.h"

#ifdef UNDER_CE
#include <winuserm.h>
#endif

#include "../Common/StringConvert.h"

#include "Clipboard.h"
#include "Defs.h"
#include "MemoryGlobal.h"
#include "Shell.h"
#include "../Common/MyCom.h"
#include "com.h"

namespace NWindows {

bool CClipboard::Open(HWND wndNewOwner) throw()
{
  m_Open = BOOLToBool(::OpenClipboard(wndNewOwner));
  return m_Open;
}

bool CClipboard::Close() throw()
{
  if (!m_Open)
    return true;
  m_Open = !BOOLToBool(CloseClipboard());
  return !m_Open;
}

bool ClipboardIsFormatAvailableHDROP()
{
  return BOOLToBool(IsClipboardFormatAvailable(CF_HDROP));
}

/*
bool ClipboardGetTextString(AString &s)
{
  s.Empty();
  if (!IsClipboardFormatAvailable(CF_TEXT))
    return false;
  CClipboard clipboard;

  if (!clipboard.Open(NULL))
    return false;

  HGLOBAL h = ::GetClipboardData(CF_TEXT);
  if (h != NULL)
  {
    NMemory::CGlobalLock globalLock(h);
    const char *p = (const char *)globalLock.GetPointer();
    if (p != NULL)
    {
      s = p;
      return true;
    }
  }
  return false;
}
*/

/*
bool ClipboardGetFileNames(UStringVector &names)
{
  names.Clear();
  if (!IsClipboardFormatAvailable(CF_HDROP))
    return false;
  CClipboard clipboard;

  if (!clipboard.Open(NULL))
    return false;

  HGLOBAL h = ::GetClipboardData(CF_HDROP);
  if (h != NULL)
  {
    NMemory::CGlobalLock globalLock(h);
    void *p = (void *)globalLock.GetPointer();
    if (p != NULL)
    {
      NShell::CDrop drop(false);
      drop.Attach((HDROP)p);
      drop.QueryFileNames(names);
      return true;
    }
  }
  return false;
}
*/

static bool ClipboardSetData(UINT uFormat, const void *data, size_t size) throw()
{
  NMemory::CGlobal global;
  if (!global.Alloc(GMEM_DDESHARE | GMEM_MOVEABLE, size))
    return false;
  {
    NMemory::CGlobalLock globalLock(global);
    LPVOID p = globalLock.GetPointer();
    if (!p)
      return false;
    memcpy(p, data, size);
  }
  if (::SetClipboardData(uFormat, global) == NULL)
    return false;
  global.Detach();
  return true;
}

bool ClipboardSetText(HWND owner, const UString &s)
{
  CClipboard clipboard;
  if (!clipboard.Open(owner))
    return false;
  if (!::EmptyClipboard())
    return false;

  bool res;
  res = ClipboardSetData(CF_UNICODETEXT, (const wchar_t *)s, (s.Len() + 1) * sizeof(wchar_t));
  #ifndef _UNICODE
  AString a (UnicodeStringToMultiByte(s, CP_ACP));
  if (ClipboardSetData(CF_TEXT, (const char *)a, (a.Len() + 1) * sizeof(char)))
    res = true;
  a = UnicodeStringToMultiByte(s, CP_OEMCP);
  if (ClipboardSetData(CF_OEMTEXT, (const char *)a, (a.Len() + 1) * sizeof(char)))
    res = true;
  #endif
  return res;
}

class EnumFormatEtc : public IEnumFORMATETC
{
public:
  HRESULT __stdcall QueryInterface(REFIID iid, void** ppvObject);
  ULONG __stdcall AddRef();
  ULONG __stdcall Release();

  HRESULT __stdcall Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched);
  HRESULT __stdcall Skip(ULONG celt);
  HRESULT __stdcall Reset();
  HRESULT __stdcall Clone(IEnumFORMATETC** ppEnumFormatEtc);

  EnumFormatEtc(UInt32 numFormats, FORMATETC* pFormatEtc);
  ~EnumFormatEtc();

private:
  int refCount;
  UInt32 index;
  UInt32 count;
  FORMATETC* fmtetc;
};

HRESULT __stdcall EnumFormatEtc::QueryInterface(REFIID iid, void** ppvObject)
{
  if (iid == IID_IEnumFORMATETC || iid == IID_IUnknown)
  {
    AddRef();
    *ppvObject = this;
    return S_OK;
  }
  else
  {
    *ppvObject = NULL;
    return E_NOINTERFACE;
  }
}
ULONG __stdcall EnumFormatEtc::AddRef()
{
  return InterlockedIncrement((LONG*) &refCount);
}
ULONG __stdcall EnumFormatEtc::Release()
{
  int count = InterlockedDecrement((LONG*) &refCount);
  if (count == 0)
    delete this;
  return count;
}

HRESULT __stdcall EnumFormatEtc::Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched)
{
  if (celt == 0 || rgelt == NULL)
    return E_INVALIDARG;
  UInt32 copied = 0;
  while (index < count && copied < celt)
  {
    rgelt[copied] = fmtetc[index];
    if (rgelt[copied].ptd)
    {
      rgelt[copied].ptd = (DVTARGETDEVICE*) CoTaskMemAlloc(sizeof(DVTARGETDEVICE));
      *rgelt[copied].ptd = *fmtetc[index].ptd;
    }
    index++;
    copied++;
  }
  if (pceltFetched)
    *pceltFetched = copied;
  return (copied == celt ? S_OK : S_FALSE);
}
HRESULT __stdcall EnumFormatEtc::Skip(ULONG celt)
{
  index += celt;
  return (index <= count ? S_OK : S_FALSE);
}
HRESULT __stdcall EnumFormatEtc::Reset()
{
  index = 0;
  return S_OK;
}
HRESULT __stdcall EnumFormatEtc::Clone(IEnumFORMATETC** ppEnumFormatEtc)
{
  EnumFormatEtc* clone = new EnumFormatEtc(count, fmtetc);
  clone->index = index;
  *ppEnumFormatEtc = clone;
  return S_OK;
}

EnumFormatEtc::EnumFormatEtc(UInt32 numFormats, FORMATETC* pFormatEtc)
{
  refCount = 1;
  index = 0;
  count = numFormats;
  if (count)
  {
    fmtetc = new FORMATETC[count];
    for (UInt32 i = 0; i < count; i++)
    {
      fmtetc[i] = pFormatEtc[i];
      if (fmtetc[i].ptd)
      {
        fmtetc[i].ptd = (DVTARGETDEVICE*) CoTaskMemAlloc(sizeof(DVTARGETDEVICE));
        *fmtetc[i].ptd = *pFormatEtc[i].ptd;
      }
    }
  }
  else
    fmtetc = NULL;
}
EnumFormatEtc::~EnumFormatEtc()
{
  for (UInt32 i = 0; i < count; i++)
    if (fmtetc[i].ptd)
      CoTaskMemFree(fmtetc[i].ptd);
  delete[] fmtetc;
}


class DataObject : public IDataObject
{
public:
  HRESULT __stdcall QueryInterface(REFIID iid, void** ppvObject);
  ULONG __stdcall AddRef();
  ULONG __stdcall Release();

  HRESULT __stdcall GetData(FORMATETC* pFormatEtc, STGMEDIUM* pMedium);
  HRESULT __stdcall GetDataHere(FORMATETC* pFormatEtc, STGMEDIUM* pMedium);
  HRESULT __stdcall QueryGetData(FORMATETC* pFormatEtc);
  HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC* pFormatEtc, FORMATETC* pFormatEtcOut);
  HRESULT __stdcall SetData(FORMATETC* pFormatEtc, STGMEDIUM* pMedium, BOOL fRelease);
  HRESULT __stdcall EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnumFormatEtc);
  HRESULT __stdcall DAdvise(FORMATETC* pFormatEtc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection);
  HRESULT __stdcall DUnadvise(DWORD dwConnection);
  HRESULT __stdcall EnumDAdvise(IEnumSTATDATA** ppEnumAdvise);

  DataObject(CLIPFORMAT format, HGLOBAL data);
  ~DataObject();

private:
  int refCount;
  FORMATETC fmtetc;
  STGMEDIUM stgmed;
};

HRESULT __stdcall DataObject::QueryInterface(REFIID iid, void** ppvObject)
{
  if (iid == IID_IDataObject || iid == IID_IUnknown)
  {
    AddRef();
    *ppvObject = this;
    return S_OK;
  }
  else
  {
    *ppvObject = NULL;
    return E_NOINTERFACE;
  }
}
ULONG __stdcall DataObject::AddRef()
{
  return InterlockedIncrement((LONG*) &refCount);
}
ULONG __stdcall DataObject::Release()
{
  int count = InterlockedDecrement((LONG*) &refCount);
  if (count == 0)
    delete this;
  return count;
}

HRESULT __stdcall DataObject::GetData(FORMATETC* pFormatEtc, STGMEDIUM* pMedium)
{
  if (fmtetc.cfFormat != pFormatEtc->cfFormat || fmtetc.dwAspect != pFormatEtc->dwAspect ||
      (fmtetc.tymed & pFormatEtc->tymed) == 0)
    return DV_E_FORMATETC;
  pMedium->tymed = fmtetc.tymed;
  pMedium->pUnkForRelease = NULL;
  if (fmtetc.tymed == TYMED_HGLOBAL)
  {
    UInt32 len = GlobalSize(stgmed.hGlobal);
    void* source = GlobalLock(stgmed.hGlobal);
    pMedium->hGlobal = GlobalAlloc(GMEM_FIXED, len);
    memcpy((void*) pMedium->hGlobal, source, len);
    GlobalUnlock(stgmed.hGlobal);
    return S_OK;
  }
  else
    return DV_E_FORMATETC;
}
HRESULT __stdcall DataObject::GetDataHere(FORMATETC* pFormatEtc, STGMEDIUM* pMedium)
{
  return DV_E_FORMATETC;
}
HRESULT __stdcall DataObject::QueryGetData(FORMATETC* pFormatEtc)
{
  if (fmtetc.cfFormat != pFormatEtc->cfFormat || fmtetc.dwAspect != pFormatEtc->dwAspect ||
      (fmtetc.tymed & pFormatEtc->tymed) == 0)
    return DV_E_FORMATETC;
  return S_OK;
}
HRESULT __stdcall DataObject::GetCanonicalFormatEtc(FORMATETC* pFormatEtc, FORMATETC* pFormatEtcOut)
{
  pFormatEtcOut->ptd = NULL;
  return E_NOTIMPL;
}
HRESULT __stdcall DataObject::SetData(FORMATETC* pFormatEtc, STGMEDIUM* pMedium, BOOL fRelease)
{
  return E_NOTIMPL;
}
HRESULT __stdcall DataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnumFormatEtc)
{
  if (dwDirection == DATADIR_GET)
  {
    *ppEnumFormatEtc = new NWindows::EnumFormatEtc(1, &fmtetc);
    return S_OK;
  }
  else
    return E_NOTIMPL;
}
HRESULT __stdcall DataObject::DAdvise(FORMATETC* pFormatEtc, DWORD advf, IAdviseSink* pAdvSink,
                                      DWORD* pdwConnection)
{
  return OLE_E_ADVISENOTSUPPORTED;
}
HRESULT __stdcall DataObject::DUnadvise(DWORD dwConnection)
{
  return OLE_E_ADVISENOTSUPPORTED;
}
HRESULT __stdcall DataObject::EnumDAdvise(IEnumSTATDATA** ppEnumAdvise)
{
  return OLE_E_ADVISENOTSUPPORTED;
}

DataObject::DataObject(CLIPFORMAT format, HGLOBAL data)
{
  refCount = 1;
  fmtetc.cfFormat = format;
  fmtetc.ptd = NULL;
  fmtetc.dwAspect = DVASPECT_CONTENT;
  fmtetc.lindex = -1;
  fmtetc.tymed = TYMED_HGLOBAL;
  stgmed.tymed = TYMED_HGLOBAL;
  stgmed.hGlobal = data;
  stgmed.pUnkForRelease = NULL;
}
DataObject::~DataObject()
{
  GlobalFree(stgmed.hGlobal);
}

void ClipboardSetFiles(HWND owner, const std::vector<std::wstring>& filePaths, DWORD effect)
{
  UNREFERENCED_PARAMETER(owner);

  NCOM::CStgMedium medium;
  CMyComPtr<IDataObject> clipboardObject;

  HRESULT hr = S_OK;
  // hr = OleGetClipboard(&clipboardObject);

  // if (FAILED(hr))
  // {
  //   return;
  // }

  // SHCreateDataObject(NULL, 0, NULL, NULL, IID_IDataObject, (void **)&clipboardObject);


  DROPFILES *dropFiles = nullptr;
  PDWORD pdwEffect = NULL;

  // Allocate memory for the DROPFILES structure
  size_t fileBufferSize = 0;
  for (const auto &filePath : filePaths)
  {
    fileBufferSize += filePath.length() + 1;
  }
  // For the second null terminator.
  fileBufferSize += 1;

  medium.hGlobal = GlobalAlloc(GMEM_MOVEABLE | GHND | GMEM_SHARE, sizeof(DROPFILES) + fileBufferSize * sizeof(wchar_t));
  if (medium.hGlobal == nullptr)
  {
    goto cleanup;
  }

  dropFiles = static_cast<DROPFILES *>(GlobalLock(medium.hGlobal));
  if (dropFiles == nullptr)
  {
    goto cleanup;
  }

  dropFiles->pFiles = sizeof(DROPFILES);
  dropFiles->pt.x = 0;
  dropFiles->pt.y = 0;
  dropFiles->fNC = FALSE;
  dropFiles->fWide = TRUE;

  wchar_t *fileBuffer = reinterpret_cast<wchar_t *>(dropFiles + 1);
  wchar_t *fileBufferPos = fileBuffer;
  for (const auto &filePath : filePaths)
  {
    int ret = wcsncpy_s(fileBufferPos, fileBufferSize - (fileBufferPos - fileBuffer),
              filePath.c_str(), filePath.length());
    if (ret != 0)
    {
      MessageBox(0, L"HI", L"HI", 0);
    }
    fileBufferPos += filePath.length() + 1;
  }
  // __debugbreak();
  fileBufferPos[0] = '\0';
  GlobalUnlock(medium.hGlobal);

  clipboardObject = new DataObject(CF_HDROP, medium.hGlobal);

  // NShell::DataObject_SetData_HGLOBAL(clipboardObject.get(), CF_HDROP, medium);

  medium.hGlobal = NULL;
  dropFiles = nullptr;


  medium.hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
  if (medium.hGlobal == nullptr)
  {
    goto cleanup;
  }

  pdwEffect = (DWORD *)GlobalLock(medium.hGlobal);
  if (pdwEffect == nullptr)
  {
    return;
  }
  *pdwEffect = effect;
  GlobalUnlock(medium.hGlobal);
  pdwEffect = NULL;

  // hr = NShell::DataObject_SetData_HGLOBAL(clipboardObject.get(), (CLIPFORMAT)
  //     RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT), medium);
  // if (FAILED(hr))
  // {
  //   return;
  // }
  medium.hGlobal = NULL;

  OleSetClipboard(clipboardObject.get());
cleanup:
  if (medium.hGlobal != NULL)
  {
    GlobalFree(medium.hGlobal);
  }
}


void ClipboardSetFiles2(HWND owner, const std::vector<std::wstring>& filePaths, DWORD effect)
{
  (VOID)owner;
  (VOID)effect;

  // from #include <ShlObj.h>
  DROPFILES *dropFiles = nullptr;
  HGLOBAL hGlobal = NULL;

  // Open the clipboard
  if (!OpenClipboard(nullptr))
  {
    return;
  }
  // Clear the clipboard
  EmptyClipboard();

  // Allocate memory for the DROPFILES structure
  size_t fileBufferSize = 0;
  for (const auto &filePath : filePaths)
  {
    fileBufferSize += filePath.length() + 1;
  }
  // For the second null terminator.
  fileBufferSize += 1;

  hGlobal = GlobalAlloc(GMEM_MOVEABLE | GHND | GMEM_SHARE, sizeof(DROPFILES) + fileBufferSize * sizeof(wchar_t));
  if (hGlobal == nullptr)
  {
    goto cleanup;
  }

  dropFiles = static_cast<DROPFILES *>(GlobalLock(hGlobal));
  if (dropFiles == nullptr)
  {
    goto cleanup;
  }

  dropFiles->pFiles = sizeof(DROPFILES);
  dropFiles->pt.x = 0;
  dropFiles->pt.y = 0;
  dropFiles->fNC = FALSE;
  dropFiles->fWide = TRUE;

  wchar_t *fileBuffer = reinterpret_cast<wchar_t *>(dropFiles + 1);
  wchar_t *fileBufferPos = fileBuffer;
  for (const auto &filePath : filePaths)
  {
    int ret = wcsncpy_s(fileBufferPos, fileBufferSize - (fileBufferPos - fileBuffer),
              filePath.c_str(), filePath.length());
    if (ret != 0)
    {
      MessageBox(0, L"HI", L"HI", 0);
    }
    fileBufferPos += filePath.length() + 1;
  }
  // __debugbreak();
  fileBufferPos[0] = '\0';
  GlobalUnlock(hGlobal);

  SetClipboardData(CF_HDROP, hGlobal);
  hGlobal = NULL;
  dropFiles = nullptr;

cleanup:
  if (hGlobal != NULL)
  {
    GlobalFree(hGlobal);
  }
  CloseClipboard();
}


void SetFORMATETC(FORMATETC *pftc, CLIPFORMAT cfFormat, DVTARGETDEVICE *ptd, DWORD dwAspect, LONG lindex, DWORD tymed)
{
  pftc->cfFormat = cfFormat;
  pftc->tymed = tymed;
  pftc->lindex = lindex;
  pftc->dwAspect = dwAspect;
  pftc->ptd = ptd;
}

void ClipboardGetFiles(HWND owner, UStringVector& filePaths, DWORD& effect)
{
  NCOM::CStgMedium medium;
  CMyComPtr<IDataObject> clipboardObject;

  HRESULT hr = OleGetClipboard(&clipboardObject);

  if (FAILED(hr))
  {
    return;
  }

  hr = NShell::DataObject_GetData_HDROP_or_IDLIST_Names(clipboardObject.get(), filePaths);
  if (FAILED(hr))
  {
    return;
  }

  hr = NShell::DataObject_GetData_HGLOBAL(clipboardObject.get(), (CLIPFORMAT)
      RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT), medium);
  if (FAILED(hr))
  {
    return;
  }

  PDWORD pdwEffect = (DWORD *) GlobalLock(medium.hGlobal);
  if (pdwEffect == nullptr)
  {
    return;
  }

  if (*pdwEffect != DROPEFFECT_NONE)
  {
    effect = *pdwEffect;
  }
  GlobalUnlock(medium.hGlobal);
}

void ClipboardGetFiles2(HWND owner, std::vector<std::wstring>& filePaths)
{
  HANDLE hDrop = NULL;
  HDROP hDropInfo = nullptr;

  if (!OpenClipboard(owner))
  {
    return;
  }

  if (IsClipboardFormatAvailable(CF_HDROP))
  {
    goto cleanup;
  }

  hDrop = GetClipboardData(CF_HDROP);
  if (hDrop == nullptr)
  {
    goto cleanup;
  }

  hDropInfo = static_cast<HDROP>(GlobalLock(hDrop));
  if (hDropInfo == nullptr)
  {
    goto cleanup;
  }

  UINT numFiles = DragQueryFileW(hDropInfo, 0xFFFFFFFF, nullptr, 0);

  // Loop through the files and retrieve their names
  for (UINT i = 0; i < numFiles; ++i)
  {
    UINT size = DragQueryFileW(hDropInfo, i, nullptr, 0);
    std::wstring fileName(size + 1, L'\0');
    DragQueryFileW(hDropInfo, i, &fileName[0], size + 1);
    filePaths.push_back(fileName);
  }

  // Unlock the handle
  GlobalUnlock(hDrop);
  hDrop = nullptr;

cleanup:
  if (hDropInfo != nullptr)
  {
    GlobalUnlock(hDrop);
  }

  if (hDrop != NULL)
  {
    GlobalFree(hDrop);
  }

  CloseClipboard();
}

}