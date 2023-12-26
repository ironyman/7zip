#include "StdAfx.h"
#include <Shlwapi.h>
#include "Path.h"

std::optional<std::wstring> ExpandEnvironmentStringsWrapper(const std::wstring &sourceString)
{
	auto length = ExpandEnvironmentStrings(sourceString.c_str(), nullptr, 0);

	if (length == 0)
	{
		return std::nullopt;
	}

	std::wstring expandedString;
	expandedString.resize(length);

	length = ExpandEnvironmentStrings(sourceString.c_str(), expandedString.data(), length);

	if (length == 0)
	{
		return std::nullopt;
	}

	// length includes the terminating NULL character, which shouldn't be included in the actual
	// string.
	expandedString.resize(length - 1);

	return expandedString;
}
