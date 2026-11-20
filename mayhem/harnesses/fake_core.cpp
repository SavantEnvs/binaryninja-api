// Minimal harness-side stand-in for the closed Binary Ninja core (libbinaryninjacore), just enough
// to drive the open-source demangler plugins through their registered BNDemanglerCallbacks.
// Every other core function comes from upstream's generated no-op stubs (stubs/generate_stubs.py),
// which build.sh compiles as weak symbols; the definitions here override them.
//
// State kept across inputs: the callback struct captured once at CorePluginInit and the static
// dummy handles below. Neither is written per input, so each input's result depends only on it.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "binaryninjacore.h"
#include "fake_core.h"

static BNDemanglerCallbacks g_callbacks;
static bool g_registered;
static char g_platform, g_arch, g_handle, g_demangler;

const BNDemanglerCallbacks* FakeCoreDemangler() { return g_registered ? &g_callbacks : nullptr; }
BNPlatform* FakeCorePlatform() { return (BNPlatform*)&g_platform; }

extern "C" {
BNDemangler* BNRegisterDemangler(const char*, const BNDemanglerCallbacks* callbacks)
{
	g_callbacks = *callbacks;
	g_registered = true;
	return (BNDemangler*)&g_demangler;
}

// A 64-bit platform with 4-byte ints (what DemanglerFallbackArchitecture in
// demangler/demangled_type_node.cpp assumes).
BNArchitecture* BNGetPlatformArchitecture(BNPlatform*) { return (BNArchitecture*)&g_arch; }
size_t BNGetArchitectureAddressSize(BNArchitecture*) { return 8; }
size_t BNGetArchitectureDefaultIntegerSize(BNArchitecture*) { return 4; }
BNEndianness BNGetArchitectureEndianness(BNArchitecture*) { return LittleEndian; }
BNPlatform* BNNewPlatformReference(BNPlatform* p) { return p; }
BNArchitecture* BNNewArchitectureReference(BNArchitecture* a) { return a; }

// Strings handed across the API boundary must be real heap strings.
char* BNAllocString(const char* s) { return strdup(s ? s : ""); }
char* BNAllocStringWithLength(const char* s, size_t len)
{
	char* r = (char*)malloc(len + 1);
	if (len)
		memcpy(r, s, len);
	r[len] = 0;
	return r;
}
void BNFreeString(char* s) { free(s); }

// Log output is dropped unless BN_FAKE_CORE_LOG is set (debugging aid; read-only, per process).
static void vlog(const char* fmt, va_list ap)
{
	static const bool on = getenv("BN_FAKE_CORE_LOG") != nullptr;
	if (!on)
		return;
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
}
void BNLog(size_t, BNLogLevel, const char*, size_t, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}
void BNLogWithStackTrace(size_t, BNLogLevel, const char*, size_t, const char*, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}
char** BNAllocStringList(const char** list, size_t count)
{
	char** r = (char**)malloc(sizeof(char*) * (count ? count : 1));
	for (size_t i = 0; i < count; i++)
		r[i] = strdup(list[i] ? list[i] : "");
	return r;
}
void BNFreeStringList(char** list, size_t count)
{
	for (size_t i = 0; i < count; i++)
		free(list[i]);
	free(list);
}
}
