#pragma once
#include <vector>
#include "binaryninjacore.h"
const std::vector<BNCustomBinaryViewType>& FakeCoreViewTypes();
BNBinaryView* FakeCoreRawView(const uint8_t* data, size_t len);
bool FakeCoreInitView(BNBinaryView* view);
void FakeCoreFreeViews(BNBinaryView* raw, BNBinaryView* created);
extern "C" bool CorePluginInit();
