#ifndef ZIP7_INC_DEBUG_H
#define ZIP7_INC_DEBUG_H
#include <type_traits>
#include <set>
#include <string>
#include "MyWindows.h"

extern std::set<std::wstring> g_debugTags;

VOID
DbgDumpHex(PBYTE pbData, SIZE_T cbData);
void DbgDumpRange(PVOID begin, PVOID end, PCSTR format, ...);

void __cdecl Z7DbgPrintA(const char *format, ...);
void __cdecl Z7DbgPrintW(const wchar_t *format, ...);
void __cdecl Z7DbgPrintEx(const wchar_t *tag, const wchar_t *format, ...);
void Z7DbgEnableTag(const wchar_t *tag);

// template <const wchar_t*... Types>
// void Z7DbgEnableTags(const wchar_t* arg, Types... rest)
// {
//   Z7DbgEnableTag(arg);
//   if constexpr (sizeof...(rest) > 0)
//   {
//       Z7DbgEnableTags(rest...);
//   }
// }

template <typename T, typename... Args>
constexpr bool AllAreSame() {
  return (std::is_same_v<T, Args> && ...);
}

template <typename... Types>
std::enable_if_t<AllAreSame<const wchar_t*, Types...>(), void>
Z7DbgEnableTags(const wchar_t* arg, Types... rest)
{
  Z7DbgEnableTag(arg);
  if constexpr (sizeof...(rest) > 0)
  {
    Z7DbgEnableTags(rest...);
  }
}

template<typename... Args>
void __cdecl Z7DbgPrintEx(const wchar_t *tag, const wchar_t *format, Args... args)
{
  if (g_debugTags.find(tag) == g_debugTags.end())
  {
    return;
  }

  Z7DbgPrintW(L"[%s] ", tag);
  Z7DbgPrintW(format, args...);
}

// This works but we want to restrict to const wchar_t* rest
// template <class... Types>
// void Z7DbgEnableTags(const wchar_t* arg, Types... rest)
// {
//   Z7DbgEnableTag(arg);
//   if constexpr (sizeof...(rest) > 0)
//   {
//       Z7DbgEnableTags(rest...);
//   }
// }

#endif