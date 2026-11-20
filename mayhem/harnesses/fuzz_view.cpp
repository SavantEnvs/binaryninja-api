// Fuzz a BinaryViewType loader plugin (ELF, PE/COFF/TE, ... — chosen at build time by which
// plugin source is linked) through the interface the core uses: CorePluginInit() registers the
// view types; for each registered type the harness asks isValidForData on a raw view of the input,
// then create(), then the created view's init callback, then releases the views.
#include <cstdint>
#include <cstddef>
#include "fake_view_core.h"

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	CorePluginInit();
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	BNBinaryView* raw = FakeCoreRawView(data, size);
	for (const BNCustomBinaryViewType& t : FakeCoreViewTypes())
	{
		if (!t.isValidForData(t.context, raw))
			continue;
		BNBinaryView* view = t.create(t.context, raw);
		if (view)
			FakeCoreInitView(view);
		FakeCoreFreeViews(raw, view);
		raw = FakeCoreRawView(data, size);
	}
	FakeCoreFreeViews(raw, nullptr);
	return 0;
}
