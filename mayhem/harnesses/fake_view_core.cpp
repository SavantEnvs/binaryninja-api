// Harness-side stand-in for the closed core's data layer, for the BinaryViewType loader plugins:
// a raw parent view backed by the fuzz input, DataBuffer, BinaryReader, and capture of the
// BNRegisterBinaryViewType / BNCreateCustomBinaryView registrations. Everything the loaders then
// emit (segments, sections, symbols, types) goes to the weak no-op stubs. Reads through the
// loader's own (virtual) view return no data: segment mapping lives in the real core.
// Per-input state (views, readers, buffers) is created and freed inside each input by the harness.
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <vector>
#include "binaryninjacore.h"
#include "fake_view_core.h"

struct FakeView
{
	bool raw;
	const uint8_t* data;
	size_t len;
	BNBinaryView* parent;
	BNCustomBinaryView cb;
};
struct FakeReader
{
	FakeView* view;
	uint64_t pos;
	BNEndianness endian;
	uint64_t virtualBase;
};
struct FakeBuffer
{
	std::vector<uint8_t> bytes;
};

static std::vector<BNCustomBinaryViewType> g_viewTypes;
static std::vector<FakeView*> g_customViews;
static char g_file, g_viewType;

static FakeView* V(BNBinaryView* v) { return reinterpret_cast<FakeView*>(v); }
static FakeBuffer* D(BNDataBuffer* b)
{
	// A NULL buffer (a no-op stub's answer) reads as empty.
	static FakeBuffer empty;
	if (!b)
	{
		empty.bytes.clear();
		return &empty;
	}
	return reinterpret_cast<FakeBuffer*>(b);
}
static FakeReader* R(BNBinaryReader* r) { return reinterpret_cast<FakeReader*>(r); }

const std::vector<BNCustomBinaryViewType>& FakeCoreViewTypes() { return g_viewTypes; }

BNBinaryView* FakeCoreRawView(const uint8_t* data, size_t len)
{
	return reinterpret_cast<BNBinaryView*>(new FakeView{true, data, len, nullptr, {}});
}

void FakeCoreFreeViews(BNBinaryView* raw, BNBinaryView* created)
{
	// Release the view create() returned the way the core does when its last reference goes away.
	// A view whose C++ constructor threw was already destroyed by the unwinding, so its context is
	// not called back.
	for (FakeView* v : g_customViews)
	{
		if (reinterpret_cast<BNBinaryView*>(v) == created && v->cb.freeObject)
			v->cb.freeObject(v->cb.context);
		delete v;
	}
	g_customViews.clear();
	delete V(raw);
}

bool FakeCoreInitView(BNBinaryView* view)
{
	FakeView* v = V(view);
	return v && !v->raw && v->cb.init && v->cb.init(v->cb.context);
}

static size_t readRaw(FakeView* v, void* dest, uint64_t offset, size_t len)
{
	if (!v || !v->raw || !dest || !len || offset >= v->len)
		return 0;
	size_t n = v->len - offset < len ? (size_t)(v->len - offset) : len;
	memcpy(dest, v->data + offset, n);
	return n;
}

extern "C" {
BNBinaryViewType* BNRegisterBinaryViewType(const char*, const char*, BNCustomBinaryViewType* type)
{
	g_viewTypes.push_back(*type);
	return reinterpret_cast<BNBinaryViewType*>(&g_viewType);
}
BNBinaryView* BNCreateCustomBinaryView(const char*, BNFileMetadata*, BNBinaryView* parent, BNCustomBinaryView* cb)
{
	FakeView* v = new FakeView{false, nullptr, 0, parent, *cb};
	g_customViews.push_back(v);
	return reinterpret_cast<BNBinaryView*>(v);
}
BNBinaryView* BNNewViewReference(BNBinaryView* view) { return view; }
void BNFreeBinaryView(BNBinaryView*) {}
BNFileMetadata* BNGetFileForView(BNBinaryView*) { return reinterpret_cast<BNFileMetadata*>(&g_file); }
BNFileMetadata* BNNewFileReference(BNFileMetadata* f) { return f; }
BNBinaryView* BNGetParentView(BNBinaryView* view) { return view ? V(view)->parent : nullptr; }
size_t BNReadViewData(BNBinaryView* view, void* dest, uint64_t offset, size_t len) { return readRaw(V(view), dest, offset, len); }
BNDataBuffer* BNReadViewBuffer(BNBinaryView* view, uint64_t offset, size_t len)
{
	FakeBuffer* b = new FakeBuffer;
	FakeView* v = V(view);
	if (v && v->raw && offset < v->len)
	{
		size_t n = v->len - offset < len ? (size_t)(v->len - offset) : len;
		b->bytes.assign(v->data + offset, v->data + offset + n);
	}
	return reinterpret_cast<BNDataBuffer*>(b);
}
uint64_t BNGetViewLength(BNBinaryView* view) { return view && V(view)->raw ? V(view)->len : 0; }
uint64_t BNGetStartOffset(BNBinaryView*) { return 0; }
uint64_t BNGetEndOffset(BNBinaryView* view) { return BNGetViewLength(view); }
bool BNIsValidOffset(BNBinaryView* view, uint64_t offset) { return offset < BNGetViewLength(view); }
bool BNIsOffsetReadable(BNBinaryView* view, uint64_t offset) { return offset < BNGetViewLength(view); }
BNEndianness BNGetDefaultEndianness(BNBinaryView*) { return LittleEndian; }

BNDataBuffer* BNCreateDataBuffer(const void* data, size_t len)
{
	FakeBuffer* b = new FakeBuffer;
	if (data && len)
		b->bytes.assign((const uint8_t*)data, (const uint8_t*)data + len);
	else
		b->bytes.resize(len);
	return reinterpret_cast<BNDataBuffer*>(b);
}
BNDataBuffer* BNDuplicateDataBuffer(BNDataBuffer* buf) { return reinterpret_cast<BNDataBuffer*>(new FakeBuffer(*D(buf))); }
void BNFreeDataBuffer(BNDataBuffer* buf) { delete reinterpret_cast<FakeBuffer*>(buf); }
void* BNGetDataBufferContents(BNDataBuffer* buf) { return D(buf)->bytes.data(); }
void* BNGetDataBufferContentsAt(BNDataBuffer* buf, size_t offset) { return D(buf)->bytes.data() + offset; }
size_t BNGetDataBufferLength(BNDataBuffer* buf) { return D(buf)->bytes.size(); }
BNDataBuffer* BNGetDataBufferSlice(BNDataBuffer* buf, size_t start, size_t len)
{
	auto& s = D(buf)->bytes;
	if (start > s.size())
		start = s.size();
	if (len > s.size() - start)
		len = s.size() - start;
	return BNCreateDataBuffer(s.data() + start, len);
}
void BNSetDataBufferLength(BNDataBuffer* buf, size_t len) { D(buf)->bytes.resize(len); }
void BNClearDataBuffer(BNDataBuffer* buf) { std::fill(D(buf)->bytes.begin(), D(buf)->bytes.end(), 0); }
void BNSetDataBufferContents(BNDataBuffer* buf, void* data, size_t len) { D(buf)->bytes.assign((uint8_t*)data, (uint8_t*)data + len); }
void BNAssignDataBuffer(BNDataBuffer* dest, BNDataBuffer* src) { D(dest)->bytes = D(src)->bytes; }
void BNAppendDataBuffer(BNDataBuffer* dest, BNDataBuffer* src) { auto c = D(src)->bytes; D(dest)->bytes.insert(D(dest)->bytes.end(), c.begin(), c.end()); }
void BNAppendDataBufferContents(BNDataBuffer* dest, const void* src, size_t len) { D(dest)->bytes.insert(D(dest)->bytes.end(), (const uint8_t*)src, (const uint8_t*)src + len); }
uint8_t BNGetDataBufferByte(BNDataBuffer* buf, size_t offset) { return D(buf)->bytes[offset]; }
void BNSetDataBufferByte(BNDataBuffer* buf, size_t offset, uint8_t val) { D(buf)->bytes[offset] = val; }

BNBinaryReader* BNCreateBinaryReader(BNBinaryView* view) { return reinterpret_cast<BNBinaryReader*>(new FakeReader{V(view), 0, LittleEndian, 0}); }
void BNFreeBinaryReader(BNBinaryReader* r) { delete R(r); }
BNEndianness BNGetBinaryReaderEndianness(BNBinaryReader* r) { return R(r)->endian; }
void BNSetBinaryReaderEndianness(BNBinaryReader* r, BNEndianness e) { R(r)->endian = e; }
bool BNReadData(BNBinaryReader* r, void* dest, size_t len)
{
	FakeReader* f = R(r);
	size_t n = readRaw(f->view, dest, f->pos - f->virtualBase, len);
	if (n != len)
		return false;
	f->pos += len;
	return true;
}
}

template <typename T>
static bool readInt(BNBinaryReader* r, T* out, int endian)
{
	uint8_t b[sizeof(T)];
	if (!BNReadData(r, b, sizeof(T)))
		return false;
	bool big = endian < 0 ? R(r)->endian == BigEndian : endian == 1;
	T v = 0;
	for (size_t i = 0; i < sizeof(T); i++)
		v |= (T)b[big ? i : sizeof(T) - 1 - i] << (8 * (sizeof(T) - 1 - i));
	*out = v;
	return true;
}
extern "C" {
bool BNRead8(BNBinaryReader* r, uint8_t* out) { return BNReadData(r, out, 1); }
bool BNRead16(BNBinaryReader* r, uint16_t* out) { return readInt(r, out, -1); }
bool BNRead32(BNBinaryReader* r, uint32_t* out) { return readInt(r, out, -1); }
bool BNRead64(BNBinaryReader* r, uint64_t* out) { return readInt(r, out, -1); }
bool BNReadLE16(BNBinaryReader* r, uint16_t* out) { return readInt(r, out, 0); }
bool BNReadLE32(BNBinaryReader* r, uint32_t* out) { return readInt(r, out, 0); }
bool BNReadLE64(BNBinaryReader* r, uint64_t* out) { return readInt(r, out, 0); }
bool BNReadBE16(BNBinaryReader* r, uint16_t* out) { return readInt(r, out, 1); }
bool BNReadBE32(BNBinaryReader* r, uint32_t* out) { return readInt(r, out, 1); }
bool BNReadBE64(BNBinaryReader* r, uint64_t* out) { return readInt(r, out, 1); }
uint64_t BNGetReaderPosition(BNBinaryReader* r) { return R(r)->pos; }
void BNSeekBinaryReader(BNBinaryReader* r, uint64_t offset) { R(r)->pos = offset; }
void BNSeekBinaryReaderRelative(BNBinaryReader* r, int64_t offset) { R(r)->pos += offset; }
uint64_t BNGetBinaryReaderVirtualBase(BNBinaryReader* r) { return R(r)->virtualBase; }
void BNSetBinaryReaderVirtualBase(BNBinaryReader* r, uint64_t base) { R(r)->virtualBase = base; }
bool BNIsEndOfFile(BNBinaryReader* r) { return R(r)->pos - R(r)->virtualBase >= BNGetViewLength(reinterpret_cast<BNBinaryView*>(R(r)->view)); }
}

extern "C" char** BNGetUniqueSectionNames(BNBinaryView*, const char** names, size_t count)
{
	// The core de-duplicates; returning the names unchanged is enough for the loaders' bookkeeping.
	return BNAllocStringList(names, count);
}

// The real core always has architectures registered for the loaders' machine types (x86/x86_64 at
// least, which PEView falls back to unconditionally). Hand out one dummy architecture and platform;
// their getters come from fake_core.cpp (64-bit, little-endian) or the no-op stubs.
static char g_viewArch, g_viewPlatform;
extern "C" BNArchitecture* BNGetArchitectureForViewType(BNBinaryViewType*, uint32_t, BNEndianness)
{
	return reinterpret_cast<BNArchitecture*>(&g_viewArch);
}
extern "C" BNPlatform* BNGetArchitectureStandalonePlatform(BNArchitecture*)
{
	return reinterpret_cast<BNPlatform*>(&g_viewPlatform);
}
extern "C" BNPlatform* BNGetAssociatedPlatformByAddress(BNPlatform* platform, uint64_t*) { return platform; }
extern "C" BNArchitecture* BNGetAssociatedArchitectureByAddress(BNArchitecture* arch, uint64_t*) { return arch; }
// Metadata key/value stores are returned as a struct the API iterates; the real core never returns NULL.
extern "C" BNMetadataValueStore* BNMetadataGetValueStore(BNMetadata*) { return new BNMetadataValueStore{0, nullptr, nullptr}; }
extern "C" void BNFreeMetadataValueStore(BNMetadataValueStore* data) { delete data; }
// No type libraries are loaded: the core reports a miss as NULL (and leaves *lib untouched).
extern "C" BNType* BNBinaryViewImportTypeLibraryType(BNBinaryView*, BNTypeLibrary**, BNQualifiedName*) { return nullptr; }
extern "C" BNType* BNBinaryViewImportTypeLibraryObject(BNBinaryView*, BNTypeLibrary**, BNQualifiedName*) { return nullptr; }
extern "C" BNType* BNBinaryViewImportTypeLibraryTypeByGuid(BNBinaryView*, const char*) { return nullptr; }
