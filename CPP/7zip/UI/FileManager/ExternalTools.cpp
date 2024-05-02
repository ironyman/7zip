#include "StdAfx.h"
#include "../../../Windows/ProcessUtils.h"
#include <functional>
#include <cstdio>

void StartFzf(UString searchPath, std::function<void(UString)> callback)
{
  auto findProc = new NWindows::CProcess();
  findProc->_overlapWindow = TRUE;
  findProc->_readOutput = TRUE;

  // Doubly null terminated string, last null is for list of null terminated strings.
  // If you're setting this in shell it would be
  // $env:FZF_DEFAULT_COMMAND = 'rg --hidden --no-ignore -l --max-depth 5 ""'
  UString envStr = L"FZF_DEFAULT_COMMAND=rg --hidden --no-ignore -l --max-depth 5 \"\"";
  auto env = CRecordVector<WCHAR>(envStr.Ptr(), envStr.Ptr() + envStr.Len() + 1); // Include null terminator
  env.Add(0);

  UString pipeName = NWindows::MyGetNextPipeName();
  int backSlash = pipeName.ReverseFind_PathSepar();
  constexpr WCHAR cmdTemplate[] = LR"(
powershell -command " & { $output = fzf;
$pipeName    = '%s';
$npipeClient = new-object System.IO.Pipes.NamedPipeClientStream('.', $pipeName, [System.IO.Pipes.PipeDirection]::Out,
[System.IO.Pipes.PipeOptions]::None,
[System.Security.Principal.TokenImpersonationLevel]::Impersonation);
$npipeClient.Connect();
$script:pipeWriter = new-object System.IO.StreamWriter($npipeClient);
$pipeWriter.AutoFlush = $true;
$pipeWriter.WriteLine($output);
$pipeWriter.Close(); }"
  )";
  WCHAR cmd[4096]{};

#pragma warning(disable : 4774)

  _snwprintf_s(cmd, ARRAYSIZE(cmd), ARRAYSIZE(cmd), cmdTemplate, pipeName.Ptr() + backSlash + 1);

  // No newlines allowed.
  for (auto &ch : cmd)
  {
    if (ch == '\n')
    {
      ch = ' ';
    }
  }

  // Use conhost, it's faster.
  findProc->_createPipeOnly = TRUE;
  findProc->Create(L"conhost", cmd, searchPath, (LPVOID)env.begin());

  // Use default terminal, need to set
  // findProc->_createPipeOnly = FALSE
  // findProc->Create(L"fzf.exe", L"", searchPath, (LPVOID)env.begin());

  // to test
  // findProc->Create(L"cmd.exe", L"", searchPath, (LPVOID)env.Ptr());
  // findProc->Create(L"conhost", L"fzf.exe", searchPath);
  // findProc->Create(L"conhost", L"powershell -noexit -command fzf.exe", searchPath);
  // findProc->Create(L"conhost", L"powershell -command \" & { $output = fzf; [Console]::Error.WriteLine($output) }\"", searchPath, (LPVOID)env.begin());

  if (findProc->WaitAndRun(callback) != 0)
  {
    delete findProc;
  }
  // On success, findProc is freed after callback is called.

  // auto path = findProc.WaitRead();

  // if (!PathIsDirectory(path))
  // {
  //   path = path.GetDirectory();
  // }
  // OnNotifyComboBoxEnter(path);
}

void StartExternalConsoleCommandToReadOutput(UString searchPath, UString envStr, UString consoleCommand, std::function<void(UString)> callback)
{
  auto findProc = new NWindows::CProcess();
  findProc->_overlapWindow = TRUE;
  findProc->_readOutput = TRUE;

  auto env = CRecordVector<WCHAR>(envStr.Ptr(), envStr.Ptr() + envStr.Len() + 1); // Include null terminator
  env.Add(0);

  UString pipeName = NWindows::MyGetNextPipeName();
  int backSlash = pipeName.ReverseFind_PathSepar();
  constexpr WCHAR cmdTemplate[] = LR"(
powershell -command " & { $output = %s;
$pipeName    = '%s';
$npipeClient = new-object System.IO.Pipes.NamedPipeClientStream('.', $pipeName, [System.IO.Pipes.PipeDirection]::Out,
[System.IO.Pipes.PipeOptions]::None,
[System.Security.Principal.TokenImpersonationLevel]::Impersonation);
$npipeClient.Connect();
$script:pipeWriter = new-object System.IO.StreamWriter($npipeClient);
$pipeWriter.AutoFlush = $true;
$pipeWriter.WriteLine($output);
$pipeWriter.Close(); }"
  )";
  WCHAR cmd[4096]{};

#pragma warning(disable : 4774)

  _snwprintf_s(cmd, ARRAYSIZE(cmd), ARRAYSIZE(cmd), cmdTemplate, consoleCommand.Ptr_non_const(), pipeName.Ptr() + backSlash + 1);

  for (auto &ch : cmd)
  {
    if (ch == '\n')
    {
      ch = ' ';
    }
  }

  findProc->_createPipeOnly = TRUE;
  findProc->Create(L"conhost", cmd, searchPath, (LPVOID)env.begin());


  if (findProc->WaitAndRun(callback) != 0)
  {
    delete findProc;
  }
}

// This doesn't work because WaitAndRun expect a read pipe to be setup.
void StartExternalConsoleCommand(UString searchPath, UString envStr, UString consoleCommand, std::function<void(UString)> callback)
{
  auto findProc = new NWindows::CProcess();
  findProc->_overlapWindow = TRUE;

  auto env = CRecordVector<WCHAR>(envStr.Ptr(), envStr.Ptr() + envStr.Len() + 1); // Include null terminator
  env.Add(0);

  findProc->Create(L"conhost", consoleCommand.Ptr_non_const(), searchPath, (LPVOID)env.begin());


  if (findProc->WaitAndRun(callback) != 0)
  {
    delete findProc;
  }
}

