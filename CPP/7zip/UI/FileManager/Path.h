#pragma once
#include <optional>
#include <string>

std::optional<std::wstring> ExpandEnvironmentStringsWrapper(const std::wstring &sourceString);