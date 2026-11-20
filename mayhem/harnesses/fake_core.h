#pragma once
#include "binaryninjacore.h"
const BNDemanglerCallbacks* FakeCoreDemangler();
BNPlatform* FakeCorePlatform();
extern "C" bool CorePluginInit();
