#pragma once

#include "../Config.h"

namespace RR::Patches {
    // Called once by main.cpp's startup worker after configuration validation
    // and loaded-module fingerprint checks. This is not a DllMain callback:
    // it waits for Unity API slots, resolves DNS and allocates native call gates.
    // Zero means queued hooks were enabled; nonzero leaves a startup diagnostic.
    // Settings are copied into hook state before any callback can run.
    DWORD Initialize(const RR::Config::Settings& settings);
}
