#include "StdAfx.h"
#include "App.h"
#include <shlwapi.h>

void CApp::MoveSubWindowsMultiPanel()
{
  HWND hWnd = _window;
  RECT rect;
  if (!hWnd)
    return;
  ::GetClientRect(hWnd, &rect);
  int xSize = rect.right;
  if (xSize == 0)
    return;
  int headerSize = 0;

  if (_toolBar)
  {
    _toolBar.AutoSize();
    RECT toolbarRect{};
    _toolBar.GetWindowRect(&toolbarRect);
    // headerSize += Window_GetRealHeight(_toolBar);
    // Effectively the same.
    headerSize += RECT_SIZE_Y(toolbarRect);
  }

  int ySize = MyMax((int)(rect.bottom - headerSize), 0);
  static const int kSplitterWidth = 4;

  int xWidth0 = xSize / 3 - kSplitterWidth / 2;
  int panel1StartX = xSize / 3 + kSplitterWidth / 2;
  int panel2StartX = xSize / 3 * 2 + kSplitterWidth / 2;
  Panels[2].Move(panel2StartX, headerSize, xWidth0, ySize);
  Panels[1].Move(panel1StartX, headerSize, xWidth0, ySize);
  Panels[0].Move(0,            headerSize, xWidth0, ySize);

  // Remove left over artifacts from resizing smaller.
  InvalidateRect(Panels[1]._mainWindow, NULL, TRUE);
}

HRESULT CApp::InitializeMultiPanel()
{
  NumPanels = 3;
  // There's a weird case in CApp::Create where it doesn't initialize the first panel
  // so just initialize all of them here.
  for (int i = 0; i < kNumPanelsMax; ++i)
  {
    COpenResult openRes;
    RINOK(CreateOnePanel(i, UString(), UString(),
                         false, // needOpenArc
                         openRes));
    // List mode
    Panels[i].SetListViewMode(3);
    Panels[i].Show(SW_SHOWNORMAL);
    Panels[i].Enable(true);

    // Initialize columns to only the name column
    bool foundName = false;
    for (int j = Panels[i]._visibleColumns.Size() - 1; j >= 0; --j)
    {
      if (Panels[i]._visibleColumns[j].Name.Compare(L"Name") != 0)
      {
        Panels[i].DeleteColumn(j);
      }
      else
      {
        foundName = true;
      }
    }

    if (!foundName)
    {
      for (auto const& column : Panels[i]._columns)
      {
        Panels[i].AddColumn(column);
      }
    }
  }

  Panels[1].SetFocusToList();
  Panels[1]._listView.SetItemState_Selected(0, true);
  MultiPanelMode = 1;

  SyncMultiPanel();
  MoveSubWindowsMultiPanel();

  return S_OK;
}

HRESULT CApp::UninitializeMultiPanel()
{
  while (NumPanels > 1)
  {
    Panels[NumPanels - 1].Enable(false);
    Panels[NumPanels - 1].Show(SW_HIDE);
    --NumPanels;
  }
  LastFocusedPanel = 0;
  MultiPanelMode = 0;

  MoveSubWindows();
  return S_OK;
}

HRESULT CApp::SyncMultiPanel()
{
  auto cwd = Panels[1].GetFsPath();
  auto parent = Panels[1].GetParentDirPrefix(); // could use std::filesystem::parent_path:
  UString preview;

  if (parent.Len() > 0 && parent[parent.Len()-1] == L':')
  {
    parent.Add_PathSepar();
  }
  CRecordVector<UInt32> selected;
  Panels[1].Get_ItemIndices_Selected(selected);

  if (selected.Size() > 0)
  {
    preview = Panels[1].GetItemFullPath(selected[0]).Ptr();
  }

  // Update parent panel
  if (Panels[0].PanelCreated)
  {
    if (parent.Compare(cwd) != 0 && parent.Compare(L"Computer") != 0)
    {
      if (Panels[0].GetFsPath() != parent)
      {
        Panels[0].BindToPathAndRefresh(parent);
      }
    }
    else
    {
      Panels[0].BindToPathAndRefresh(L"");
      Panels[0].DeleteListItems();
    }
  }

  // Update preview panel
  if (Panels[2].PanelCreated)
  {
    if (preview.Len() > 0 && PathIsDirectory(preview) && Panels[2].PanelCreated && preview != Panels[2].GetFsPath())
    {
      Panels[2].BindToPathAndRefresh(preview);
    }
    else
    {
      Panels[2].BindToPathAndRefresh(L"");
      Panels[2].DeleteListItems();
    }
  }
  return S_OK;
}

HRESULT CPanelCallbackImp::OnRefreshList(bool& shouldReturn)
{
  (void)shouldReturn;
  return S_OK;
}

HRESULT CPanelCallbackImp::OnBind(bool& shouldReturn)
{
  (void)shouldReturn;
  return S_OK;
}

HRESULT CPanelCallbackImp::OnSelectedItemChanged()
{

  if (_app->MultiPanelMode == 0)
  {
    return S_OK;
  }

  if (_index != 1)
  {
    return S_OK;
  }

  if (multiPanelReentrancyCount++ == 0)
  {
    _app->SyncMultiPanel();
    --multiPanelReentrancyCount;
  }
  return S_OK;
}

HRESULT CPanelCallbackImp::OnOpenFolder(std::optional<std::reference_wrapper<bool>> shouldReturn)
{
  if (_app->MultiPanelMode == 0)
  {
    return S_OK;
  }

  if (shouldReturn.has_value())
  {
    shouldReturn->get() = false;
  }

  if (_index != 1)
  {
    auto cwd = _app->Panels[_index].GetFsPath();
    _app->Panels[1].BindToPathAndRefresh(cwd);

    // This is to notify OnNotifyComboBoxEnter to skip default action.
    if (shouldReturn.has_value())
    {
      shouldReturn->get() = true;
    }
  }

  _app->Panels[1]._appState->FolderHistory.AddString(_app->Panels[1]._currentFolderPrefix);
  _app->SyncMultiPanel();

  _app->Panels[1]._listView.SetItemState_Selected(0, true);
  _app->Panels[1].SetFocusToList();

  return S_OK;
}

HRESULT CPanelCallbackImp::OnOpenParentFolder()
{
  if (_app->MultiPanelMode == 0)
  {
    return S_OK;
  }

  _app->SyncMultiPanel();

  return S_OK;
}

UString CPanelCallbackImp::OnSetComboText(UString const& text)
{
  if (_app->MultiPanelMode == 0 || _index == 0)
  {
    return UString();
  }

  // The second and third panel should only display the file name.
  return text.GetFileName();

  // _app->Panels[1].SetFocusToList();
}

bool CPanelCallbackImp::IsMultiPanelMode()
{
  return _app->MultiPanelMode != 0;
}