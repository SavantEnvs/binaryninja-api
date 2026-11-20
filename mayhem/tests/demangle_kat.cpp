// Known-answer driver for mayhem/test.sh: one mangled name per stdin line -> the qualified name the
// demangler plugin returns through its registered callbacks ("<mangled?> <a::b::c>" or "<mangled?> FAIL").
#include <cstdio>
#include <string>
#include "fake_core.h"

int main()
{
	CorePluginInit();
	const BNDemanglerCallbacks* cb = FakeCoreDemangler();
	if (!cb)
		return 2;
	char line[4096];
	while (fgets(line, sizeof(line), stdin)) {
		std::string name(line);
		while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
			name.pop_back();
		int mangled = cb->isMangledString(cb->context, name.c_str()) ? 1 : 0;
		BNDemanglerConfig config = {FakeCorePlatform(), nullptr, true};
		BNDemanglerResult result = {};
		if (!cb->demangle(cb->context, name.c_str(), &config, &result)) {
			printf("%d FAIL\n", mangled);
			continue;
		}
		std::string q;
		for (size_t i = 0; i < result.name.nameCount; i++)
			q += (i ? std::string(result.name.join ? result.name.join : "::") : std::string()) + (result.name.name[i] ? result.name.name[i] : "");
		printf("%d %s\n", mangled, q.c_str());
		cb->freeResult(cb->context, &result);
	}
	return 0;
}
