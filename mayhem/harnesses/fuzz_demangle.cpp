// Fuzz a demangler plugin (GNU3 or MSVC, chosen at build time by which plugin source is linked)
// through the interface the core uses: CorePluginInit() registers BNDemanglerCallbacks, and the
// harness calls isMangledString + demangle + freeResult on the input as a NUL-terminated name.
// Byte 0 bit0 toggles template simplification (BNDemanglerConfig.simplifyTemplates).
#include <cstdint>
#include <cstddef>
#include <string>
#include "fake_core.h"

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	CorePluginInit();
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	const BNDemanglerCallbacks* cb = FakeCoreDemangler();
	if (!cb || size < 1)
		return -1;
	std::string name(reinterpret_cast<const char*>(data + 1), size - 1);
	name = name.c_str();  // the core passes C strings: stop at the first NUL
	(void)cb->isMangledString(cb->context, name.c_str());
	BNDemanglerConfig config = {FakeCorePlatform(), nullptr, (data[0] & 1) != 0};
	BNDemanglerResult result = {};
	if (cb->demangle(cb->context, name.c_str(), &config, &result))
		cb->freeResult(cb->context, &result);
	return 0;
}
