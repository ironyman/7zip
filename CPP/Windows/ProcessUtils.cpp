// ProcessUtils.cpp

#include "StdAfx.h"

#include "../Common/StringConvert.h"
#include "Thread.h"

#include "ProcessUtils.h"
#include <shlwapi.h>
#include <cstdio>

#ifndef _UNICODE
extern bool g_IsNT;
#endif

#include "../7zip/UI/FileManager/App.h"

namespace NWindows {

static volatile long PipeSerialNumber;


UString MyGetNextPipeName()
{
  CHAR PipeNameBuffer[ MAX_PATH ];
  snprintf( PipeNameBuffer, MAX_PATH,
           "\\\\.\\Pipe\\7zFMPipe.%08lx.%08lx",
           GetCurrentProcessId(),
           PipeSerialNumber + 1
         );
  return UString(PipeNameBuffer);
}

BOOL
APIENTRY
MyCreatePipeEx(
    OUT LPHANDLE lpReadPipe,
    OUT LPHANDLE lpWritePipe,
    IN LPSECURITY_ATTRIBUTES lpPipeAttributes,
    IN DWORD nSize,
    DWORD dwReadMode,
    DWORD dwWriteMode
    )
/*++
Routine Description:
    The CreatePipeEx API is used to create an anonymous pipe I/O device.
    Unlike CreatePipe FILE_FLAG_OVERLAPPED may be specified for one or
    both handles.
    Two handles to the device are created.  One handle is opened for
    reading and the other is opened for writing.  These handles may be
    used in subsequent calls to ReadFile and WriteFile to transmit data
    through the pipe.
Arguments:
    lpReadPipe - Returns a handle to the read side of the pipe.  Data
        may be read from the pipe by specifying this handle value in a
        subsequent call to ReadFile.
    lpWritePipe - Returns a handle to the write side of the pipe.  Data
        may be written to the pipe by specifying this handle value in a
        subsequent call to WriteFile.
    lpPipeAttributes - An optional parameter that may be used to specify
        the attributes of the new pipe.  If the parameter is not
        specified, then the pipe is created without a security
        descriptor, and the resulting handles are not inherited on
        process creation.  Otherwise, the optional security attributes
        are used on the pipe, and the inherit handles flag effects both
        pipe handles.
    nSize - Supplies the requested buffer size for the pipe.  This is
        only a suggestion and is used by the operating system to
        calculate an appropriate buffering mechanism.  A value of zero
        indicates that the system is to choose the default buffering
        scheme.
Return Value:
    TRUE - The operation was successful.
    FALSE/NULL - The operation failed. Extended error status is available
        using GetLastError.
--*/
{
  HANDLE ReadPipeHandle = NULL, WritePipeHandle = NULL;
  DWORD dwError;
  CHAR PipeNameBuffer[ MAX_PATH ];

  //
  // Only one valid OpenMode flag - FILE_FLAG_OVERLAPPED
  //

  if ((dwReadMode | dwWriteMode) & (~FILE_FLAG_OVERLAPPED)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  //
  //  Set the default timeout to 120 seconds
  //

  if (nSize == 0) {
    nSize = 4096;
  }

  snprintf( PipeNameBuffer, 256,
           "\\\\.\\pipe\\7zFMPipe.%08lx.%08lx",
           GetCurrentProcessId(),
           InterlockedIncrement(&PipeSerialNumber)
         );

  // For PIPE_ACCESS_INBOUND, the writer must open with GENERIC_WRITE
  // or [System.IO.Pipes.PipeDirection]::Out.
  // If you create with PIPE_ACCESS_DUPLEX then
  // you can open with GENERIC_ALL or [System.IO.Pipes.PipeDirection]::InOut.
  ReadPipeHandle = CreateNamedPipeA(
          PipeNameBuffer,             // pipe name
          PIPE_ACCESS_INBOUND | dwReadMode,       // read/write access
          PIPE_TYPE_MESSAGE |       // message type pipe
          PIPE_READMODE_MESSAGE |   // message-read mode
          PIPE_WAIT,                // blocking mode
          PIPE_UNLIMITED_INSTANCES, // max. instances
          nSize,                  // output buffer size
          nSize,                  // input buffer size
          0,                        // client time-out
          NULL);                    // default security attribute

  if (! ReadPipeHandle) {
    return FALSE;
  }

  *lpReadPipe = ReadPipeHandle;

  if (lpWritePipe != NULL)
  {
    WritePipeHandle = CreateFileA(
                        PipeNameBuffer,
                        GENERIC_WRITE,
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        lpPipeAttributes,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | dwWriteMode,
                        NULL                       // Template file
                      );

    if (INVALID_HANDLE_VALUE == WritePipeHandle) {
      dwError = GetLastError();
      CloseHandle( ReadPipeHandle );
      SetLastError(dwError);
      return FALSE;
    }
    *lpWritePipe = WritePipeHandle;
  }

  return( TRUE );
}


#ifndef UNDER_CE
static UString GetQuotedString(const UString &s)
{
  UString s2 ('\"');
  s2 += s;
  s2 += '\"';
  return s2;
}
#endif

WRes CProcess::Create(LPCWSTR imageName, const UString &params, LPCWSTR curDir, LPVOID additionalEnvVar)
{
  /*
  OutputDebugStringW(L"CProcess::Create");
  OutputDebugStringW(imageName);
  if (params)
  {
    OutputDebugStringW(L"params:");
    OutputDebugStringW(params);
  }
  if (curDir)
  {
    OutputDebugStringW(L"cur dir:");
    OutputDebugStringW(curDir);
  }
  */

  Close();
  const UString params2 =
      #ifndef UNDER_CE
      GetQuotedString(imageName) + L' ' +
      #endif
      params;
  #ifdef UNDER_CE
  curDir = NULL;
  #else
  imageName = NULL;
  #endif
  PROCESS_INFORMATION pi;
  BOOL result;
  #ifndef _UNICODE
  if (!g_IsNT)
  {
    STARTUPINFOA si;
    si.cb = sizeof(si);
    si.lpReserved = NULL;
    si.lpDesktop = NULL;
    si.lpTitle = NULL;
    si.dwFlags = 0;
    si.cbReserved2 = 0;
    si.lpReserved2 = NULL;

    CSysString curDirA;
    if (curDir != 0)
      curDirA = GetSystemString(curDir);
    const AString s = GetSystemString(params2);
    result = ::CreateProcessA(NULL, s.Ptr_non_const(),
        NULL, NULL, FALSE, 0, NULL, ((curDir != 0) ? (LPCSTR)curDirA: 0), &si, &pi);
  }
  else
  #endif
  {
    // Need to allow inheritance for child process to access pipe.
    SECURITY_ATTRIBUTES sa {
      sizeof(SECURITY_ATTRIBUTES), NULL, TRUE
    };

    CRecordVector<WCHAR> env;
    STARTUPINFOW si{};
    si.cb = sizeof(si);

    if (_readOutput)
    {
      // if (!CreatePipe(&_hStdoutRead, &_hStdoutWrite, &sa, 0))
      // {
      //   return (WRes)-1;
      // }

      // NOTE: uncomment this and use previous if you want to use WaitAndRunOverlapped, but it has issues.
      if (!MyCreatePipeEx(
          &_hStdoutRead, _createPipeOnly == TRUE ? NULL : &_hStdoutWrite, &sa, 0,
          0, 0)
      )
      {
        return (WRes)-1;
      }
      if (!_createPipeOnly)
      {
        si.hStdOutput = _hStdoutWrite;
        si.hStdError = _hStdoutWrite;
        si.dwFlags |= STARTF_USESTDHANDLES;
      }
    }

    if (_overlapWindow)
    {
      RECT rc;
      g_App._window.GetWindowRect(&rc);
      si.dwX = rc.left;
      si.dwY = rc.top;

      // size doesn't work for some reason
      si.dwXSize = rc.right - rc.left;
      si.dwYSize = rc.bottom - rc.top;
      si.dwFlags |= STARTF_USEPOSITION | STARTF_USESIZE;
    }

    if (!PathIsDirectory(curDir))
    {
      curDir = NULL;
    }

    if (additionalEnvVar != NULL)
    {
      PWCHAR envBlock = GetEnvironmentStrings();
      PWCHAR envBlockEnd = envBlock;

      // Could probably just SetEnvironmentVariable instead.
      while (*envBlockEnd != 0)
      {
        envBlockEnd += wcslen(envBlockEnd) + 1;
      }

      PWCHAR additionalEnvVarEnd = (PWCHAR)additionalEnvVar;
      while (*additionalEnvVarEnd != 0)
      {
        additionalEnvVarEnd += wcslen(additionalEnvVarEnd) + 1;
      }

      env = CRecordVector<WCHAR>(envBlock, envBlockEnd);

      // DbgDumpRange(env.begin(), env.end(), "env after adding envBlock");
      // DbgDumpRange(additionalEnvVar, additionalEnvVarEnd, "additionalEnvVar");
      env += CRecordVector<WCHAR>((PWCHAR)additionalEnvVar, additionalEnvVarEnd);
      // DbgDumpRange(env.begin(), env.end(), "env after adding temp");
      env.Add(0);
    }

    result = CreateProcessW(imageName, params2.Ptr_non_const(),
        NULL, NULL, _readOutput && !_createPipeOnly, CREATE_UNICODE_ENVIRONMENT,
        additionalEnvVar != NULL ? (LPVOID)env.begin() : NULL,
        curDir, &si, &pi);

    // Child process will have this handle, we don't need it. When child process
    // exits they will close their instance of this handle and our _hStdoutRead
    // will be signalled.
    if (_hStdoutWrite != NULL)
    {
      CloseHandle(_hStdoutWrite);
      _hStdoutWrite = NULL;
    }

    if (result == 0)
    {
      DbgDumpRange(env.begin(), env.end(), "CreateProcessW failed with GLE %x, dumping env", GetLastError());
    }
  }

  ::CloseHandle(pi.hThread);
  _handle = pi.hProcess;

  if (result == 0)
  {
    MessageBoxW(g_HWND, L"CreateProcessW failed", L"Failed with error", MB_ICONERROR | MB_OK);
    return ::GetLastError();
  }
  return 0;
}

WRes MyCreateProcess(LPCWSTR imageName, const UString &params)
{
  CProcess process;
  return process.Create(imageName, params, NULL);
}

UString CProcess::WaitRead()
{
  DWORD dwRead;
  CHAR chBuf[4096];
  BOOL bSuccess = FALSE;

  for (;;)
  {
    bSuccess = ReadFile(_hStdoutRead, chBuf, sizeof(chBuf), &dwRead, NULL);
    if (dwRead != 0)
    {
      _readBuffer += CByteVector((unsigned char *)chBuf, (unsigned char *)chBuf + dwRead);
    }

    if (!bSuccess || dwRead == 0)
      break;
  }

  _readBuffer.Add(0);

  UString result{};
  MultiByteToUnicodeString2(result, AString((char *)_readBuffer.begin()), CP_UTF8);
  return result;
}

// Must be constructed with new since it deletes itself.
struct CThreadProcessWaitSync
{
  CHAR chBuf[4096];
  DWORD dwRead{};
  HANDLE _hFile{}; // non owning
  CByteVector _readBuffer;
  std::function<void(UString)> _fn;
  CProcess *owner{};

  CThreadProcessWaitSync() {}
  virtual ~CThreadProcessWaitSync()
  {
    if (owner)
    {
      delete owner;
      owner = nullptr;
    }
  }

  HRESULT Initialize(HANDLE hFile, std::function<void(UString)> const& fn)
  {
    _fn = fn;
    _hFile = hFile;
    return S_OK;
  }
  void Process()
  {
    BOOL bSuccess = FALSE;

    if (owner != nullptr && owner->_createPipeOnly)
    {
      BOOL fConnected = ConnectNamedPipe(_hFile, NULL) ?
          TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
      if (!fConnected)
      {
        Z7DbgPrintA("ConnectNamedPipe failed\n");
        return;
      }
    }

    for (;;)
    {
      bSuccess = ReadFile(_hFile, chBuf, sizeof(chBuf), &dwRead, NULL);
      if (dwRead != 0)
      {
        _readBuffer += CByteVector((unsigned char *)chBuf, (unsigned char *)chBuf + dwRead);
      }

      if (!bSuccess || dwRead == 0)
        break;
    }

    _readBuffer.Add(0);
    DbgDumpRange(_readBuffer.begin(), _readBuffer.end(), "_readBuffer");
    UString resultString{};
    MultiByteToUnicodeString2(resultString, AString((char *)_readBuffer.begin()), CP_UTF8);
    _fn(resultString);
  }

  static THREAD_FUNC_DECL MyThreadFunction(void *param)
  {
    try
    {
      ((CThreadProcessWaitSync *)param)->Process();
      delete static_cast<CThreadProcessWaitSync *>(param);
    }
    catch (...)
    {
      std::exception_ptr p = std::current_exception();
    }
    return 0;
  }
};


// To call this, always allocate CProcess with new.
// If this function succeeds with return value 0, then this object will delete itself.
// Otherwise caller must delete.
WRes CProcess::WaitAndRun(std::function<void(UString)> const& fn)
{
  auto waiter = new CThreadProcessWaitSync();
  HRESULT result = waiter->Initialize(_hStdoutRead, fn);

  if (FAILED(result))
  {
    delete waiter;
    return (WRes)-1;
  }

  NWindows::CThread thread;
  waiter->owner = this;
  const WRes wres = thread.Create(CThreadProcessWaitSync::MyThreadFunction, waiter);
  _deleteSelf = TRUE;
  return wres;
}


// Must be constructed with new since it deletes itself.
struct CThreadProcessWaitOverlapped
{
  OVERLAPPED overlapped{};
  CHAR chBuf[4096];
  DWORD dwRead{};
  HANDLE _hFile{}; // non owning
  CByteVector _readBuffer;
  std::function<void(UString)> _fn;
  CProcess *owner{};

  CThreadProcessWaitOverlapped(OVERLAPPED overlapped) : overlapped(overlapped) {}
  CThreadProcessWaitOverlapped() {}
  virtual ~CThreadProcessWaitOverlapped()
  {
    if (overlapped.hEvent != NULL)
    {
      CloseHandle(overlapped.hEvent);
      overlapped.hEvent = NULL;
    }

    if (owner)
    {
      delete owner;
      owner = nullptr;
    }
  }

  HRESULT Initialize(HANDLE hFile, std::function<void(UString)> const& fn)
  {
    memset(&overlapped, 0, sizeof(overlapped));
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    _fn = fn;
    _hFile = hFile;

    // _readBuffer = CByteVector{};

    if (overlapped.hEvent == NULL)
    {
      return E_FAIL;
    }
    return S_OK;
  }
  void Process()
  {
    try
    {
      BOOL bSuccess = FALSE;
      bSuccess = GetOverlappedResult(_hFile,
                              &overlapped,
                              &dwRead,
                              TRUE) ;
      if (bSuccess && dwRead != 0)
      {
        // _readBuffer.Reserve(1);
        _readBuffer += CByteVector((unsigned char *)chBuf, (unsigned char *)chBuf + dwRead);
      }

      _readBuffer.Add(0);

      UString resultString{};
      MultiByteToUnicodeString2(resultString, AString((char *)_readBuffer.begin()), CP_UTF8);
      _fn(resultString);
    }
    catch(...) { }
  }

  static THREAD_FUNC_DECL MyThreadFunction(void *param)
  {
    ((CThreadProcessWaitOverlapped *)param)->Process();
    delete static_cast<CThreadProcessWaitOverlapped *>(param);
    return 0;
  }
};

// To call this, always allocate CProcess with new.
// If this function succeeds with return value 0, then this object will delete itself.
// Otherwise caller must delete.
WRes CProcess::WaitAndRunOverlapped(std::function<void(UString)> const& fn)
{
  auto waiter = new CThreadProcessWaitOverlapped();
  HRESULT result = waiter->Initialize(_hStdoutRead, fn);

  if (FAILED(result))
  {
    delete waiter;
    return (WRes)-1;
  }

  BOOL bSuccess = ReadFile(_hStdoutRead, waiter->chBuf, sizeof(waiter->chBuf), &waiter->dwRead, &waiter->overlapped);
  if (!bSuccess && GetLastError() == ERROR_IO_PENDING)
  {
    NWindows::CThread thread;
    waiter->owner = this;
    const WRes wres = thread.Create(CThreadProcessWaitOverlapped::MyThreadFunction, waiter);
    _deleteSelf = TRUE;
    return wres;
  }
  else
  {
    // Could happen but not for fzf.
    delete waiter;
    return (WRes)-1;
  }
}
}
