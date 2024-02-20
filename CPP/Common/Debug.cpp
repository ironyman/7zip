#include <StdAfx.h>
#include <cstdarg>
#include <cstdio>

void __cdecl Z7DbgPrintA(const char *format, ...)
{
  char    buf[4096], *p = buf;
  va_list args;
  int     n;

  va_start(args, format);
  n = _vsnprintf_s(p, ARRAYSIZE(buf), sizeof buf, format, args);
  // n = _vsnprintf_s(p, ARRAYSIZE(buf), sizeof buf - 3, format, args); // buf-3 is room for CR/LF/NUL
  va_end(args);

  // p += (n < 0) ? sizeof buf - 3 : n;

  // while ( p > buf  &&  isspace(p[-1]) )
  //         *--p = '\0';

  // *p++ = '\r';
  // *p++ = '\n';
  // *p   = '\0';

  OutputDebugStringA(buf);
}

void __cdecl Z7DbgPrintW(const wchar_t *format, ...)
{
  wchar_t    buf[4096], *p = buf;
  va_list args;
  int     n;

  va_start(args, format);
  n = _vsnwprintf_s(p, ARRAYSIZE(buf), sizeof buf, format, args);
  // n = _vsnprintf_s(p, ARRAYSIZE(buf), sizeof buf - 3, format, args); // buf-3 is room for CR/LF/NUL
  va_end(args);

  // p += (n < 0) ? sizeof buf - 3 : n;

  // while ( p > buf  &&  isspace(p[-1]) )
  //         *--p = '\0';

  // *p++ = '\r';
  // *p++ = '\n';
  // *p   = '\0';

  OutputDebugStringW(buf);
  // There's no terminal
  //   wprintf(L"%s", buf);
}


VOID
DbgDumpHex(PBYTE pbData, SIZE_T cbData)
{
    ULONG i;
    SIZE_T count;
    CHAR digits[]="0123456789abcdef";
    CHAR pbLine[256];
    ULONG cbLine, cbHeader = 0;
    ULONG_PTR address;

    if(pbData == NULL && cbData != 0)
    {
        // strcat_s(pbLine, RTL_NUMBER_OF(pbLine), "<null> buffer!!!\n");
        fprintf(stderr, "<null> buffer!!!\n");
        return;
    }

    for(; cbData ; cbData -= count, pbData += count)
    {
        count = (cbData > 16) ? 16:cbData;

        cbLine = cbHeader;

        address = (ULONG_PTR)pbData;

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
        // 64 bit addresses.
        pbLine[cbLine++] = digits[(address >> 0x3c) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x38) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x34) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x30) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x2c) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x28) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x24) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x20) & 0x0f];
#endif
        pbLine[cbLine++] = digits[(address >> 0x1c) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x18) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x14) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x10) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x0c) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x08) & 0x0f];
        pbLine[cbLine++] = digits[(address >> 0x04) & 0x0f];
        pbLine[cbLine++] = digits[(address        ) & 0x0f];
        pbLine[cbLine++] = ' ';
        pbLine[cbLine++] = ' ';

        for(i = 0; i < count; i++)
        {
            pbLine[cbLine++] = digits[pbData[i]>>4];
            pbLine[cbLine++] = digits[pbData[i]&0x0f];
            if(i == 7)
            {
                pbLine[cbLine++] = ':';
            }
            else
            {
                pbLine[cbLine++] = ' ';
            }
        }

        for(; i < 16; i++)
        {
            pbLine[cbLine++] = ' ';
            pbLine[cbLine++] = ' ';
            pbLine[cbLine++] = ' ';
        }

        pbLine[cbLine++] = ' ';

        for(i = 0; i < count; i++)
        {
            if(pbData[i] < 32 || pbData[i] > 126)
            {
                pbLine[cbLine++] = '.';
            }
            else
            {
                pbLine[cbLine++] = pbData[i];
            }
        }

        pbLine[cbLine++] = 0;

        Z7DbgPrintA("%s\n", pbLine);
    }
}

void DbgDumpRange(PVOID begin, PVOID end, PCSTR format, ...)
{
  Z7DbgPrintA("[CProcess::Create] ");
  va_list args;

  va_start(args, format);
  Z7DbgPrintA(format, args);
  va_end(args);

  SIZE_T size = (SIZE_T)end - (SIZE_T)begin;
  Z7DbgPrintA(" len(%lu): 0x%p - 0x%p\n", size, begin, end);
  DbgDumpHex((PBYTE)begin, size);
}