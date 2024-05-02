#ifndef ZIP7_INC_EXTERNALTOOLS_H
#define ZIP7_INC_EXTERNALTOOLS_H

#include "../../../Common/MyString.h"
#include <functional>

void StartFzf(UString searchPath, std::function<void(UString)> callback);
void StartExternalConsoleCommand(UString searchPath, UString envStr, UString consoleCommand, std::function<void(UString)> callback);
void StartExternalConsoleCommandToReadOutput(UString searchPath, UString envStr, UString consoleCommand, std::function<void(UString)> callback);

#endif
