#include "StdAfx.h"

#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"

#include "../../../Windows/COM.h"
#include "../../../Windows/Clipboard.h"
#include "../../../Windows/Menu.h"
#include "../../../Windows/PropVariant.h"
#include "../../../Windows/PropVariantConv.h"

#include "../../PropID.h"
#include "../Common/PropIDUtils.h"
#include "../Explorer/ContextMenu.h"

#include "App.h"
#include "FormatUtils.h"
#include "LangUtils.h"
#include "ListViewDialog.h"
#include "MyLoadMenu.h"
#include "PropertyName.h"

#include "PropertyNameRes.h"
#include "resource.h"

// #define SHOW_DEBUG_PANEL_MENU

using namespace NWindows;

extern
LONG g_DllRefCount;
LONG g_DllRefCount = 0;

static const UINT kSevenZipStartMenuID = kMenuCmdID_Plugin_Start;
static const UINT kSystemStartMenuID = kMenuCmdID_Plugin_Start + 400;


#ifdef SHOW_DEBUG_PANEL_MENU
static void Print_Ptr(void *p, const char *s)
{
  char temp[32];
  ConvertUInt64ToHex((UInt64)(void *)p, temp);
  AString m;
  m += temp;
  m.Add_Space();
  m += s;
  OutputDebugStringA(m);
}
#define ODS(sz) { Print_Ptr(this, sz); }
#define ODS_U(s) { OutputDebugStringW(s); }
#else
#define ODS(sz)
#define ODS_U(s)
#endif


void CPanel::InvokeSystemCommand(const char *command)
{
  NCOM::CComInitializer comInitializer;
  if (!IsFsOrPureDrivesFolder())
    return;
  CRecordVector<UInt32> operatedIndices;
  Get_ItemIndices_Operated(operatedIndices);
  if (operatedIndices.IsEmpty())
    return;
  CMyComPtr<IContextMenu> contextMenu;
  if (CreateShellContextMenu(operatedIndices, contextMenu) != S_OK)
    return;

  CMINVOKECOMMANDINFO ci;
  ZeroMemory(&ci, sizeof(ci));
  ci.cbSize = sizeof(CMINVOKECOMMANDINFO);
  ci.hwnd = GetParent();
  ci.lpVerb = command;
  contextMenu->InvokeCommand(&ci);
}

static const char * const kSeparator = "------------------------";
static const char * const kSeparatorSmall = "----------------";

extern UString ConvertSizeToString(UInt64 value) throw();
bool IsSizeProp(UINT propID) throw();

UString GetOpenArcErrorMessage(UInt32 errorFlags);


static void AddListAscii(CListViewDialog &dialog, const char *s)
{
  dialog.Strings.Add((UString)s);
  dialog.Values.AddNew();
}

static void AddSeparator(CListViewDialog &dialog)
{
  AddListAscii(dialog, kSeparator);
}

static void AddSeparatorSmall(CListViewDialog &dialog)
{
  AddListAscii(dialog, kSeparatorSmall);
}

static void AddPropertyPair(const UString &name, const UString &val, CListViewDialog &dialog)
{
  dialog.Strings.Add(name);
  dialog.Values.Add(val);
}


static void AddPropertyString(PROPID propID, const wchar_t *nameBSTR,
    const NCOM::CPropVariant &prop, CListViewDialog &dialog)
{
  if (prop.vt != VT_EMPTY)
  {
    UString val;

    if (propID == kpidErrorFlags ||
        propID == kpidWarningFlags)
    {
      UInt32 flags = GetOpenArcErrorFlags(prop);
      if (flags == 0)
        return;
      if (flags != 0)
        val = GetOpenArcErrorMessage(flags);
    }

    if (val.IsEmpty())
    {
      if ((prop.vt == VT_UI8 || prop.vt == VT_UI4 || prop.vt == VT_UI2) && IsSizeProp(propID))
      {
        UInt64 v = 0;
        ConvertPropVariantToUInt64(prop, v);
        val = ConvertSizeToString(v);
      }
      else
        ConvertPropertyToString2(val, prop, propID, 9); // we send 9 - is ns precision
    }

    if (!val.IsEmpty())
    {
      if (propID == kpidErrorType)
      {
        AddPropertyPair(L"Open WARNING:", L"Cannot open the file as expected archive type", dialog);
      }
      AddPropertyPair(GetNameOfProperty(propID, nameBSTR), val, dialog);
    }
  }
}


static void AddPropertyString(PROPID propID, UInt64 val, CListViewDialog &dialog)
{
  NCOM::CPropVariant prop = val;
  AddPropertyString(propID, NULL, prop, dialog);
}


static inline unsigned GetHex_Upper(unsigned v)
{
  return (v < 10) ? ('0' + v) : ('A' + (v - 10));
}

static inline unsigned GetHex_Lower(unsigned v)
{
  return (v < 10) ? ('0' + v) : ('a' + (v - 10));
}

static const Byte kSpecProps[] =
{
  kpidPath,
  kpidType,
  kpidErrorType,
  kpidError,
  kpidErrorFlags,
  kpidWarning,
  kpidWarningFlags,
  kpidOffset,
  kpidPhySize,
  kpidTailSize
};

void CPanel::Properties()
{
  CMyComPtr<IGetFolderArcProps> getFolderArcProps;
  _folder.QueryInterface(IID_IGetFolderArcProps, &getFolderArcProps);
  if (!getFolderArcProps)
  {
    InvokeSystemCommand("properties");
    return;
  }

  {
    CListViewDialog message;
    // message.DeleteIsAllowed = false;
    // message.SelectFirst = false;

    CRecordVector<UInt32> operatedIndices;
    Get_ItemIndices_Operated(operatedIndices);

    if (operatedIndices.Size() == 1)
    {
      UInt32 index = operatedIndices[0];
      // message += "Item:\n");
      UInt32 numProps;
      if (_folder->GetNumberOfProperties(&numProps) == S_OK)
      {
        for (UInt32 i = 0; i < numProps; i++)
        {
          CMyComBSTR name;
          PROPID propID;
          VARTYPE varType;

          if (_folder->GetPropertyInfo(i, &name, &propID, &varType) != S_OK)
            continue;

          NCOM::CPropVariant prop;
          if (_folder->GetProperty(index, propID, &prop) != S_OK)
            continue;
          AddPropertyString(propID, name, prop, message);
        }
      }


      if (_folderRawProps)
      {
        _folderRawProps->GetNumRawProps(&numProps);
        for (UInt32 i = 0; i < numProps; i++)
        {
          CMyComBSTR name;
          PROPID propID;
          if (_folderRawProps->GetRawPropInfo(i, &name, &propID) != S_OK)
            continue;

          const void *data;
          UInt32 dataSize;
          UInt32 propType;
          if (_folderRawProps->GetRawProp(index, propID, &data, &dataSize, &propType) != S_OK)
            continue;

          if (dataSize != 0)
          {
            AString s;
            if (propID == kpidNtSecure)
              ConvertNtSecureToString((const Byte *)data, dataSize, s);
            else
            {
              const UInt32 kMaxDataSize = 64;
              if (dataSize > kMaxDataSize)
              {
                s += "data:";
                s.Add_UInt32(dataSize);
              }
              else
              {
                const bool needUpper = (dataSize <= 8)
                    && (propID == kpidCRC || propID == kpidChecksum);
                for (UInt32 k = 0; k < dataSize; k++)
                {
                  const Byte b = ((const Byte *)data)[k];
                  if (needUpper)
                  {
                    s += (char)GetHex_Upper((b >> 4) & 0xF);
                    s += (char)GetHex_Upper(b & 0xF);
                  }
                  else
                  {
                    s += (char)GetHex_Lower((b >> 4) & 0xF);
                    s += (char)GetHex_Lower(b & 0xF);
                  }
                }
              }
            }
            AddPropertyPair(GetNameOfProperty(propID, name), (UString)s.Ptr(), message);
          }
        }
      }

      AddSeparator(message);
    }
    else if (operatedIndices.Size() >= 1)
    {
      UInt64 packSize = 0;
      UInt64 unpackSize = 0;
      UInt64 numFiles = 0;
      UInt64 numDirs = 0;

      FOR_VECTOR (i, operatedIndices)
      {
        const UInt32 index = operatedIndices[i];
        unpackSize += GetItemSize(index);
        packSize += GetItem_UInt64Prop(index, kpidPackSize);
        if (IsItem_Folder(index))
        {
          numDirs++;
          numDirs += GetItem_UInt64Prop(index, kpidNumSubDirs);
          numFiles += GetItem_UInt64Prop(index, kpidNumSubFiles);
        }
        else
          numFiles++;
      }
      {
        wchar_t temp[32];
        ConvertUInt32ToString(operatedIndices.Size(), temp);
        AddPropertyPair(L"", MyFormatNew(g_App.LangString_N_SELECTED_ITEMS, temp), message);
      }

      if (numDirs != 0)
        AddPropertyString(kpidNumSubDirs, numDirs, message);
      if (numFiles != 0)
        AddPropertyString(kpidNumSubFiles, numFiles, message);
      AddPropertyString(kpidSize, unpackSize, message);
      AddPropertyString(kpidPackSize, packSize, message);

      AddSeparator(message);
    }


    /*
    AddLangString(message, IDS_PROP_FILE_TYPE);
    message += kPropValueSeparator;
    message += GetFolderTypeID();
    message.Add_LF();
    */

    {
      NCOM::CPropVariant prop;
      if (_folder->GetFolderProperty(kpidPath, &prop) == S_OK)
      {
        AddPropertyString(kpidName, L"Path", prop, message);
      }
    }

    CMyComPtr<IFolderProperties> folderProperties;
    _folder.QueryInterface(IID_IFolderProperties, &folderProperties);
    if (folderProperties)
    {
      UInt32 numProps;
      if (folderProperties->GetNumberOfFolderProperties(&numProps) == S_OK)
      {
        for (UInt32 i = 0; i < numProps; i++)
        {
          CMyComBSTR name;
          PROPID propID;
          VARTYPE vt;
          if (folderProperties->GetFolderPropertyInfo(i, &name, &propID, &vt) != S_OK)
            continue;
          NCOM::CPropVariant prop;
          if (_folder->GetFolderProperty(propID, &prop) != S_OK)
            continue;
          AddPropertyString(propID, name, prop, message);
        }
      }
    }

    if (getFolderArcProps)
    {
      CMyComPtr<IFolderArcProps> getProps;
      getFolderArcProps->GetFolderArcProps(&getProps);
      if (getProps)
      {
        UInt32 numLevels;
        if (getProps->GetArcNumLevels(&numLevels) != S_OK)
          numLevels = 0;
        for (UInt32 level2 = 0; level2 < numLevels; level2++)
        {
          {
            UInt32 level = numLevels - 1 - level2;
            UInt32 numProps;
            if (getProps->GetArcNumProps(level, &numProps) == S_OK)
            {
              const int kNumSpecProps = Z7_ARRAY_SIZE(kSpecProps);

              AddSeparator(message);

              for (Int32 i = -(int)kNumSpecProps; i < (Int32)numProps; i++)
              {
                CMyComBSTR name;
                PROPID propID;
                VARTYPE vt;
                if (i < 0)
                  propID = kSpecProps[i + kNumSpecProps];
                else if (getProps->GetArcPropInfo(level, (UInt32)i, &name, &propID, &vt) != S_OK)
                  continue;
                NCOM::CPropVariant prop;
                if (getProps->GetArcProp(level, propID, &prop) != S_OK)
                  continue;
                AddPropertyString(propID, name, prop, message);
              }
            }
          }

          if (level2 < numLevels - 1)
          {
            const UInt32 level = numLevels - 1 - level2;
            UInt32 numProps;
            if (getProps->GetArcNumProps2(level, &numProps) == S_OK)
            {
              AddSeparatorSmall(message);
              for (UInt32 i = 0; i < numProps; i++)
              {
                CMyComBSTR name;
                PROPID propID;
                VARTYPE vt;
                if (getProps->GetArcPropInfo2(level, i, &name, &propID, &vt) != S_OK)
                  continue;
                NCOM::CPropVariant prop;
                if (getProps->GetArcProp2(level, propID, &prop) != S_OK)
                  continue;
                AddPropertyString(propID, name, prop, message);
              }
            }
          }
        }

        {
          // we ERROR message for NonOpen level
              bool needSep = true;
              const int kNumSpecProps = Z7_ARRAY_SIZE(kSpecProps);
              for (Int32 i = -(int)kNumSpecProps; i < 0; i++)
              {
                CMyComBSTR name;
                const PROPID propID = kSpecProps[i + kNumSpecProps];
                NCOM::CPropVariant prop;
                if (getProps->GetArcProp(numLevels, propID, &prop) != S_OK)
                  continue;
                if (needSep)
                {
                  AddSeparator(message);
                  AddSeparator(message);
                  needSep = false;
                }
                AddPropertyString(propID, name, prop, message);
              }
        }

      }
    }

    message.Title = LangString(IDS_PROPERTIES);
    message.NumColumns = 2;
    message.Create(GetParent());
  }
}



void CPanel::EditCut()
{
  // InvokeSystemCommand("cut");
}

void CPanel::EditCopy()
{
  /*
  CMyComPtr<IGetFolderArcProps> getFolderArcProps;
  _folder.QueryInterface(IID_IGetFolderArcProps, &getFolderArcProps);
  if (!getFolderArcProps)
  {
    InvokeSystemCommand("copy");
    return;
  }
  */
  UString s;
  CRecordVector<UInt32> indices;
  std::vector<std::wstring> files;
  Get_ItemIndices_Selected(indices);
  FOR_VECTOR (i, indices)
  {
    if (i != 0)
      s += "\xD\n";
    s += GetItemName(indices[i]);
  // MessageBox(0, GetItemFullPath(indices[i]).Ptr(),GetItemFullPath(indices[i]).Ptr(),0);
    files.push_back(GetItemFullPath(indices[i]).Ptr());
  }
  // ClipboardSetText(_mainWindow, s);
  ClipboardSetFiles(files);
}

void CPanel::EditPaste()
{
  /*
  UStringVector names;
  ClipboardGetFileNames(names);
  CopyFromNoAsk(names);
  UString s;
  for (int i = 0; i < names.Size(); i++)
  {
    s += L' ';
    s += names[i];
  }

  MessageBoxW(0, s, L"", 0);
  */

  InvokeSystemCommand("paste");
}



struct CFolderPidls
{
  LPITEMIDLIST parent;
  CRecordVector<LPITEMIDLIST> items;

  CFolderPidls(): parent(NULL) {}
  ~CFolderPidls()
  {
    FOR_VECTOR (i, items)
      CoTaskMemFree(items[i]);
    CoTaskMemFree(parent);
  }
};


static HRESULT ShellFolder_ParseDisplayName(IShellFolder *shellFolder,
    HWND hwnd, const UString &path, LPITEMIDLIST *ppidl)
{
  ULONG eaten = 0;
  return shellFolder->ParseDisplayName(hwnd, NULL,
      path.Ptr_non_const(), &eaten, ppidl, NULL);
}


HRESULT CPanel::CreateShellContextMenu(
    const CRecordVector<UInt32> &operatedIndices,
    CMyComPtr<IContextMenu> &systemContextMenu)
{
  ODS("==== CPanel::CreateShellContextMenu");
  systemContextMenu.Release();
  UString folderPath = GetFsPath();

  CMyComPtr<IShellFolder> desktopFolder;
  RINOK(::SHGetDesktopFolder(&desktopFolder))
  if (!desktopFolder)
  {
    // ShowMessage("Failed to get Desktop folder");
    return E_FAIL;
  }

  CFolderPidls pidls;
  // NULL is allowed for parentHWND in ParseDisplayName()
  const HWND parentHWND_for_ParseDisplayName = GetParent();
  // if (folderPath.IsEmpty()), then ParseDisplayName returns pidls of "My Computer"
  /* win10: ParseDisplayName() supports folder path with tail slash
    ParseDisplayName() returns {
      E_INVALIDARG         : path with super path prefix "\\\\?\\"
      ERROR_FILE_NOT_FOUND : path for network share (\\server\path1\long path2") larger than MAX_PATH
    } */
  const HRESULT res = ShellFolder_ParseDisplayName(desktopFolder,
      parentHWND_for_ParseDisplayName,
      folderPath, &pidls.parent);
  if (res != S_OK)
  {
    ODS_U(folderPath);
    if (res != E_INVALIDARG)
      return res;
    if (!NFile::NName::If_IsSuperPath_RemoveSuperPrefix(folderPath))
      return res;
    RINOK(ShellFolder_ParseDisplayName(desktopFolder,
        parentHWND_for_ParseDisplayName,
        folderPath, &pidls.parent))
  }
  if (!pidls.parent)
    return E_FAIL;

  /*
  UString path2;
  NShell::GetPathFromIDList(pidls.parent, path2);
  ODS_U(path2);
  */

  if (operatedIndices.IsEmpty())
  {
    // how to get IContextMenu, if there are no selected files?
    return E_FAIL;

    /*
    xp64 :
    1) we can't use GetUIObjectOf() with (numItems == 0), it throws exception
    2) we can't use desktopFolder->GetUIObjectOf() with absolute pidls of folder
        context menu items are different in that case:
          "Open / Explorer" for folder
          "Delete" for "My Computer" icon
          "Preperties" for "System"
    */
    /*
    parentFolder = desktopFolder;
    pidls.items.AddInReserved(pidls.parent);
    pidls.parent = NULL;
    */

    // CreateViewObject() doesn't show all context menu items
    /*
    HRESULT res = parentFolder->CreateViewObject(
        GetParent(), IID_IContextMenu, (void**)&systemContextMenu);
    */
  }

  CMyComPtr<IShellFolder> parentFolder;
  RINOK(desktopFolder->BindToObject(pidls.parent,
      NULL, IID_IShellFolder, (void**)&parentFolder))
  if (!parentFolder)
    return E_FAIL;

  ODS("==== CPanel::CreateShellContextMenu pidls START");

  pidls.items.ClearAndReserve(operatedIndices.Size());
  UString fileName;
  FOR_VECTOR (i, operatedIndices)
  {
    fileName.Empty();
    Add_ItemRelPath2_To_String(operatedIndices[i], fileName);
    /* ParseDisplayName() in win10 returns:
         E_INVALIDARG : if empty name, or path with dots only: "." , ".."
         HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : if there is no such file
    */
    LPITEMIDLIST pidl = NULL;
    RINOK(ShellFolder_ParseDisplayName(parentFolder,
        parentHWND_for_ParseDisplayName,
        fileName, &pidl))
    if (!pidl)
      return E_FAIL;
    pidls.items.AddInReserved(pidl);
  }

  ODS("==== CPanel::CreateShellContextMenu pidls END");
  // Get IContextMenu for items
  RINOK(parentFolder->GetUIObjectOf(GetParent(),
      pidls.items.Size(), (LPCITEMIDLIST *)(void *)&pidls.items.Front(),
      IID_IContextMenu, NULL, (void**)&systemContextMenu))
  ODS("==== CPanel::CreateShellContextMenu GetUIObjectOf finished");
  if (!systemContextMenu)
  {
    // ShowMessage("Unable to get context menu interface");
    return E_FAIL;
  }
  return S_OK;
}


// #define SHOW_DEBUG_FM_CTX_MENU

#ifdef SHOW_DEBUG_FM_CTX_MENU

static void PrintHex(UString &s, UInt32 v)
{
  char sz[32];
  ConvertUInt32ToHex(v, sz);
  s += sz;
}

static void PrintContextStr(UString &s, IContextMenu *ctxm, unsigned i, unsigned id, const char *name)
{
  s += " | ";
  s += name;
  s += ": ";
  UString s1;
  {
    char buf[256];
    buf[0] = 0;
    const HRESULT res = ctxm->GetCommandString(i, id,
        NULL, buf, Z7_ARRAY_SIZE(buf) - 1);
    if (res != S_OK)
    {
      PrintHex(s1, res);
      s1.Add_Space();
    }
    s1 += GetUnicodeString(buf);
  }
  UString s2;
  {
    wchar_t buf2[256];
    buf2[0] = 0;
    const HRESULT res = ctxm->GetCommandString(i, id | GCS_UNICODE,
        NULL, (char *)buf2, Z7_ARRAY_SIZE(buf2) - sizeof(wchar_t));
    if (res != S_OK)
    {
      PrintHex(s2, res);
      s2.Add_Space();
    }
    s2 += buf2;
  }
  s += s1;
  if (s2.Compare(s1) != 0)
  {
    s += " Unicode: ";
    s += s2;
  }
}

static void PrintAllContextItems(IContextMenu *ctxm, unsigned num)
{
  for (unsigned i = 0; i < num; i++)
  {
    UString s;
    s.Add_UInt32(i);
    s += ": ";
    PrintContextStr(s, ctxm, i, GCS_VALIDATEA, "valid");
    PrintContextStr(s, ctxm, i, GCS_VERBA, "verb");
    PrintContextStr(s, ctxm, i, GCS_HELPTEXTA, "helptext");
    OutputDebugStringW(s);
  }
}

#endif


void CPanel::CreateSystemMenu(HMENU menuSpec,
    bool showExtendedVerbs,
    const CRecordVector<UInt32> &operatedIndices,
    CMyComPtr<IContextMenu> &systemContextMenu)
{
  systemContextMenu.Release();

  CreateShellContextMenu(operatedIndices, systemContextMenu);

  if (!systemContextMenu)
    return;

  /*
  // Set up a CMINVOKECOMMANDINFO structure.
  CMINVOKECOMMANDINFO ci;
  ZeroMemory(&ci, sizeof(ci));
  ci.cbSize = sizeof(CMINVOKECOMMANDINFO);
  ci.hwnd = GetParent();
  */

  /*
  if (Sender == GoBtn)
  {
    // Verbs that can be used are cut, paste,
    // properties, delete, and so on.
    String action;
    if (CutRb->Checked)
      action = "cut";
    else if (CopyRb->Checked)
      action = "copy";
    else if (DeleteRb->Checked)
      action = "delete";
    else if (PropertiesRb->Checked)
      action = "properties";

    ci.lpVerb = action.c_str();
    result = cm->InvokeCommand(&ci);
    if (result)
      ShowMessage(
      "Error copying file to clipboard.");

  }
  else
  */
  {
    // HMENU hMenu = CreatePopupMenu();
    CMenu popupMenu;
    CMenuDestroyer menuDestroyer(popupMenu);
    if (!popupMenu.CreatePopup())
      throw 210503;
    const HMENU hMenu = popupMenu;
    DWORD flags = CMF_EXPLORE;
    if (showExtendedVerbs)
      flags |= Z7_WIN_CMF_EXTENDEDVERBS;
    ODS("=== systemContextMenu->QueryContextMenu START");
    const HRESULT res = systemContextMenu->QueryContextMenu(hMenu, 0, kSystemStartMenuID, 0x7FFF, flags);
    ODS("=== systemContextMenu->QueryContextMenu END");
    if (SUCCEEDED(res))
    {
      #ifdef SHOW_DEBUG_FM_CTX_MENU
      PrintAllContextItems(systemContextMenu, (unsigned)res);
      #endif

      CMenu menu;
      menu.Attach(menuSpec);
      CMenuItem menuItem;
      menuItem.fMask = MIIM_SUBMENU | MIIM_TYPE | MIIM_ID;
      menuItem.fType = MFT_STRING;
      menuItem.hSubMenu = popupMenu.Detach();
      menuDestroyer.Disable();
      LangString(IDS_SYSTEM, menuItem.StringValue);
      menu.InsertItem(0, true, menuItem);
    }
    /*
    if (Cmd < 100 && Cmd != 0)
    {
      ci.lpVerb = MAKEINTRESOURCE(Cmd - 1);
      ci.lpParameters = "";
      ci.lpDirectory = "";
      ci.nShow = SW_SHOWNORMAL;
      cm->InvokeCommand(&ci);
    }
    // If Cmd is > 100 then it's one of our
    // inserted menu items.
    else
      // Find the menu item.
      for (int i = 0; i < popupMenu1->Items->Count; i++)
      {
        TMenuItem* menu = popupMenu1->Items->Items[i];
        // Call its OnClick handler.
        if (menu->Command == Cmd - 100)
          menu->OnClick(this);
      }
      // Release the memory allocated for the menu.
      DestroyMenu(hMenu);
    */
  }
}

void CPanel::CreateFileMenu(HMENU menuSpec)
{
  CreateFileMenu(menuSpec, _sevenZipContextMenu, _systemContextMenu, true); // programMenu
}

void CPanel::CreateSevenZipMenu(HMENU menuSpec,
    bool showExtendedVerbs,
    const CRecordVector<UInt32> &operatedIndices,
    int firstDirIndex,
    CMyComPtr<IContextMenu> &sevenZipContextMenu)
{
  sevenZipContextMenu.Release();

  CMenu menu;
  menu.Attach(menuSpec);
  // CMenuDestroyer menuDestroyer(menu);
  // menu.CreatePopup();

  CZipContextMenu *contextMenuSpec = new CZipContextMenu;
  CMyComPtr<IContextMenu> contextMenu = contextMenuSpec;
  // if (contextMenu.CoCreateInstance(CLSID_CZipContextMenu, IID_IContextMenu) == S_OK)
  {
    /*
    CMyComPtr<IInitContextMenu> initContextMenu;
    if (contextMenu.QueryInterface(IID_IInitContextMenu, &initContextMenu) != S_OK)
      return;
    */
    ODS("=== FileName List Add START")
    // for (unsigned y = 0; y < 10000; y++, contextMenuSpec->_fileNames.Clear())
    GetFilePaths(operatedIndices, contextMenuSpec->_fileNames);
    ODS("=== FileName List Add END")
    contextMenuSpec->Init_For_7zFM();
    contextMenuSpec->_attribs.FirstDirIndex = firstDirIndex;
    {
      DWORD flags = CMF_EXPLORE;
      if (showExtendedVerbs)
        flags |= Z7_WIN_CMF_EXTENDEDVERBS;
      const HRESULT res = contextMenu->QueryContextMenu(menu,
          0, // indexMenu
          kSevenZipStartMenuID, // first
          kSystemStartMenuID - 1, // last
          flags);
      ODS("=== contextMenu->QueryContextMenu END")
      const bool sevenZipMenuCreated = SUCCEEDED(res);
      if (sevenZipMenuCreated)
      {
        // if (res != 0)
        {
          // some "non-good" implementation of QueryContextMenu() could add some items to menu, but it return 0.
          // so we still allow these items
          sevenZipContextMenu = contextMenu;
          #ifdef SHOW_DEBUG_FM_CTX_MENU
          PrintAllContextItems(contextMenu, (unsigned)res);
          #endif
        }
      }
      else
      {
        // MessageBox_Error_HRESULT_Caption(res, L"QueryContextMenu");
      }
      // int code = HRESULT_CODE(res);
      // int nextItemID = code;
    }
  }
}

static bool IsReadOnlyFolder(IFolderFolder *folder)
{
  if (!folder)
    return false;

  bool res = false;
  {
    NCOM::CPropVariant prop;
    if (folder->GetFolderProperty(kpidReadOnly, &prop) == S_OK)
      if (prop.vt == VT_BOOL)
        res = VARIANT_BOOLToBool(prop.boolVal);
  }
  return res;
}

bool CPanel::IsThereReadOnlyFolder() const
{
  if (!_folderOperations)
    return true;
  if (IsReadOnlyFolder(_folder))
    return true;
  FOR_VECTOR (i, _parentFolders)
  {
    if (IsReadOnlyFolder(_parentFolders[i].ParentFolder))
      return true;
  }
  return false;
}

bool CPanel::CheckBeforeUpdate(UINT resourceID)
{
  if (!_folderOperations)
  {
    MessageBox_Error_UnsupportOperation();
    // resourceID = resourceID;
    // MessageBoxErrorForUpdate(E_NOINTERFACE, resourceID);
    return false;
  }

  for (int i = (int)_parentFolders.Size(); i >= 0; i--)
  {
    IFolderFolder *folder;
    if (i == (int)_parentFolders.Size())
      folder = _folder;
    else
      folder = _parentFolders[i].ParentFolder;

    if (!IsReadOnlyFolder(folder))
      continue;

    UString s;
    AddLangString(s, resourceID);
    s.Add_LF();
    AddLangString(s, IDS_OPERATION_IS_NOT_SUPPORTED);
    s.Add_LF();
    if (i == 0)
      s += GetFolderPath(folder);
    else
      s += _parentFolders[i - 1].VirtualPath;
    s.Add_LF();
    AddLangString(s, IDS_PROP_READ_ONLY);
    MessageBox_Error(s);
    return false;
  }

  return true;
}

void CPanel::CreateFileMenu(HMENU menuSpec,
    CMyComPtr<IContextMenu> &sevenZipContextMenu,
    CMyComPtr<IContextMenu> &systemContextMenu,
    bool programMenu)
{
  sevenZipContextMenu.Release();
  systemContextMenu.Release();

  const bool showExtendedVerbs = IsKeyDown(VK_SHIFT);

  CRecordVector<UInt32> operatedIndices;
  Get_ItemIndices_Operated(operatedIndices);
  const int firstDirIndex = FindDir_InOperatedList(operatedIndices);

  CMenu menu;
  menu.Attach(menuSpec);

  if (!IsArcFolder())
  {
    CreateSevenZipMenu(menu, showExtendedVerbs, operatedIndices, firstDirIndex, sevenZipContextMenu);
    // CreateSystemMenu is very slow if you call it inside ZIP archive with big number of files
    // Windows probably can parse items inside ZIP archive.
    if (g_App.ShowSystemMenu)
      CreateSystemMenu(menu, showExtendedVerbs, operatedIndices, systemContextMenu);
  }

  /*
  if (menu.GetItemCount() > 0)
    menu.AppendItem(MF_SEPARATOR, 0, (LPCTSTR)0);
  */

  CFileMenu fm;

  fm.readOnly = IsThereReadOnlyFolder();
  fm.isHashFolder = IsHashFolder();
  fm.isFsFolder = Is_IO_FS_Folder();
  fm.programMenu = programMenu;
  fm.allAreFiles = (firstDirIndex == -1);
  fm.numItems = operatedIndices.Size();

  fm.isAltStreamsSupported = false;

  if (fm.numItems == 1)
    fm.FilePath = us2fs(GetItemFullPath(operatedIndices[0]));

  if (_folderAltStreams)
  {
    if (operatedIndices.Size() <= 1)
    {
      UInt32 realIndex = (UInt32)(Int32)-1;
      if (operatedIndices.Size() == 1)
        realIndex = operatedIndices[0];
      Int32 val = 0;
      if (_folderAltStreams->AreAltStreamsSupported(realIndex, &val) == S_OK)
        fm.isAltStreamsSupported = IntToBool(val);
    }
  }
  else
  {
    if (fm.numItems == 0)
      fm.isAltStreamsSupported = IsFSFolder();
    else
      fm.isAltStreamsSupported = IsFolder_with_FsItems();
  }

  fm.Load(menu, (unsigned)menu.GetItemCount());
}

bool CPanel::InvokePluginCommand(unsigned id)
{
  return InvokePluginCommand(id, _sevenZipContextMenu, _systemContextMenu);
}

#if defined(_MSC_VER) && !defined(UNDER_CE)
#define use_CMINVOKECOMMANDINFOEX
/* CMINVOKECOMMANDINFOEX depends from (_WIN32_IE >= 0x0400) */
#endif

bool CPanel::InvokePluginCommand(unsigned id,
    IContextMenu *sevenZipContextMenu, IContextMenu *systemContextMenu)
{
  UInt32 offset;
  const bool isSystemMenu = (id >= kSystemStartMenuID);
  if (isSystemMenu)
  {
    if (!systemContextMenu)
      return false;
    offset = id - kSystemStartMenuID;
  }
  else
  {
    if (!sevenZipContextMenu)
      return false;
    offset = id - kSevenZipStartMenuID;
  }

  #ifdef use_CMINVOKECOMMANDINFOEX
    CMINVOKECOMMANDINFOEX
  #else
    CMINVOKECOMMANDINFO
  #endif
      commandInfo;

  memset(&commandInfo, 0, sizeof(commandInfo));
  commandInfo.cbSize = sizeof(commandInfo);

  commandInfo.fMask = 0
  #ifdef use_CMINVOKECOMMANDINFOEX
    | CMIC_MASK_UNICODE
  #endif
    ;

  commandInfo.hwnd = GetParent();
  commandInfo.lpVerb = (LPCSTR)(MAKEINTRESOURCE(offset));
  commandInfo.lpParameters = NULL;
  // 19.01: fixed CSysString to AString
  // MSDN suggest to send NULL: lpDirectory: This member is always NULL for menu items inserted by a Shell extension.
  const AString currentFolderA (GetAnsiString(_currentFolderPrefix));
  commandInfo.lpDirectory = (LPCSTR)(currentFolderA);
  commandInfo.nShow = SW_SHOW;

  #ifdef use_CMINVOKECOMMANDINFOEX

  commandInfo.lpParametersW = NULL;
  commandInfo.lpTitle = "";

  /*
  system ContextMenu handler supports ContextMenu subhandlers.
  so InvokeCommand() converts (command_offset) from global number to subhandler number.
  XP-64 / win10:
      system ContextMenu converts (command_offset) in lpVerb only,
      and it keeps lpVerbW unchanged.
      also explorer.exe sends 0 in lpVerbW.
  We try to keep compatibility with Windows Explorer here.
  */
  commandInfo.lpVerbW = NULL;

  const UString currentFolderUnicode = _currentFolderPrefix;
  commandInfo.lpDirectoryW = currentFolderUnicode;
  commandInfo.lpTitleW = L"";
  // commandInfo.ptInvoke.x = xPos;
  // commandInfo.ptInvoke.y = yPos;
  commandInfo.ptInvoke.x = 0;
  commandInfo.ptInvoke.y = 0;

  #endif

  HRESULT result;
  if (isSystemMenu)
    result = systemContextMenu->InvokeCommand(LPCMINVOKECOMMANDINFO(&commandInfo));
  else
    result = sevenZipContextMenu->InvokeCommand(LPCMINVOKECOMMANDINFO(&commandInfo));
  if (result == NOERROR)
  {
    KillSelection();
    return true;
  }
  else
    MessageBox_Error_HRESULT_Caption(result, L"InvokeCommand");
  return false;
}

bool CPanel::OnContextMenu(HANDLE windowHandle, int xPos, int yPos)
{
  if (::GetParent((HWND)windowHandle) == _listView)
  {
    ShowColumnsContextMenu(xPos, yPos);
    return true;
  }

  if (windowHandle != _listView)
    return false;
  /*
  POINT point;
  point.x = xPos;
  point.y = yPos;
  if (!_listView.ScreenToClient(&point))
    return false;

  LVHITTESTINFO info;
  info.pt = point;
  int index = _listView.HitTest(&info);
  */

  CRecordVector<UInt32> operatedIndices;
  Get_ItemIndices_Operated(operatedIndices);

  // negative x,y are possible for multi-screen modes.
  // x=-1 && y=-1 for keyboard call (SHIFT+F10 and others).
  if (xPos == -1 && yPos == -1)
  {
    if (operatedIndices.Size() == 0)
    {
      xPos = 0;
      yPos = 0;
    }
    else
    {
      int itemIndex = _listView.GetNextItem(-1, LVNI_FOCUSED);
      if (itemIndex == -1)
        return false;
      RECT rect;
      if (!_listView.GetItemRect(itemIndex, &rect, LVIR_ICON))
        return false;
      xPos = (rect.left + rect.right) / 2;
      yPos = (rect.top + rect.bottom) / 2;
    }
    POINT point = {xPos, yPos};
    _listView.ClientToScreen(&point);
    xPos = point.x;
    yPos = point.y;
  }

  CMenu menu;
  CMenuDestroyer menuDestroyer(menu);
  menu.CreatePopup();

  CMyComPtr<IContextMenu> sevenZipContextMenu;
  CMyComPtr<IContextMenu> systemContextMenu;
  CreateFileMenu(menu, sevenZipContextMenu, systemContextMenu, false); // programMenu

  const unsigned id = (unsigned)menu.Track(TPM_LEFTALIGN
      #ifndef UNDER_CE
      | TPM_RIGHTBUTTON
      #endif
      | TPM_RETURNCMD | TPM_NONOTIFY,
    xPos, yPos, _listView);

  if (id == 0)
    return true;

  if (id >= kMenuCmdID_Plugin_Start)
  {
    InvokePluginCommand(id, sevenZipContextMenu, systemContextMenu);
    return true;
  }
  if (ExecuteFileCommand(id))
    return true;
  return true;
}
