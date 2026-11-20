#!/usr/bin/env bash
# mayhem/build.sh — build the binaryninja-api fuzz targets: the core-free disassembler libraries
# (ARMv7, Thumb2, MIPS, PowerPC/VLE), the GNU3/MSVC demangler plugins and the ELF/PE/MD1ROM loader
# plugins (on a harness-side fake core), plus the known-answer drivers mayhem/test.sh runs.
# Re-runnable and offline: compiles in-tree sources only, into mayhem/build/ (gitignored).
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
cd "$SRC"

B="$SRC/mayhem/build"
mkdir -p "$B/fuzz" "$B/test"
# The decoders shift signed immediates/bitfields left everywhere (e.g. armv7.c `imm << 2` on negative
# offsets, vle16.c `1 << 31`); UBSan's shift-base check fires on nearly every input, so only that one
# check is relaxed. ASan and the rest of UBSan (incl. shift-exponent) stay on and halting.
UBSAN_RELAX="-fno-sanitize=shift-base"
FUZZ_FLAGS="-O1 $SANITIZER_FLAGS $UBSAN_RELAX $DEBUG_FLAGS"
# fuzzer-no-link instruments the library objects for coverage without pulling in libFuzzer's main.
COV="-fsanitize=fuzzer-no-link"

ARMV7=arch/armv7/armv7_disasm
THUMB=arch/armv7/thumb2_disasm
MIPS=arch/mips/mips
PPC=arch/powerpc/decode
PPC_SRCS="decode.c mnemonic.c names.c operands.c priv.c vle16.c vle32.c"


# --- 1) project libraries, sanitized + coverage-instrumented (fuzz build) and normal (test build)
lib() {  # lib <dir> <mode>
	local d="$1" mode="$2" C CXXF CF
	if [ "$mode" = fuzz ]; then CF="$FUZZ_FLAGS $COV"; else CF="-O2 $COVERAGE_FLAGS"; fi
	mkdir -p "$d"
	pids=()
	"$CC" $CF -c "$ARMV7/armv7.c" -I"$ARMV7" -o "$d/armv7.o" & pids+=($!)
	"$CC" $CF -c "$MIPS/mips.c" -I"$MIPS" -o "$d/mips.o" & pids+=($!)
	"$CXX" -std=c++17 $CF -c "$THUMB/disassembler.cpp" -I"$THUMB" -I"$ARMV7" -o "$d/thumb_disassembler.o" & pids+=($!)
	"$CXX" -std=c++17 $CF -c "$THUMB/spec.cpp" -I"$THUMB" -I"$ARMV7" -o "$d/thumb_spec.o" & pids+=($!)
	for s in $PPC_SRCS; do
		"$CC" $CF -c "$PPC/$s" -I"$PPC" -o "$d/ppc_${s%.c}.o" & pids+=($!)
	done
	for p in "${pids[@]}"; do wait "$p"; done
}
lib "$B/fuzz" fuzz
lib "$B/test" test
PPC_OBJS() { for s in $PPC_SRCS; do printf '%s ' "$1/ppc_${s%.c}.o"; done; }

# --- 2) harnesses: libFuzzer binary + standalone reproducer, LSan off
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.c -o "$B/lsan_off.o"
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.o"

H=mayhem/harnesses
link() {  # link <name> <compiler> <harness> <include flags> <objs...>
	local name="$1" cc="$2" src="$3" inc="$4"; shift 4
	"$cc" $FUZZ_FLAGS $COV $inc -c "$src" -o "$B/$name.o"
	"$cc" $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$B/$name.o" "$@" "$B/lsan_off.o" -o "$SRC/$name"
	"$cc" $FUZZ_FLAGS "$B/standalone_main.o" "$B/$name.o" "$@" "$B/lsan_off.o" -o "$SRC/$name-standalone"
}
link fuzz_armv7 "$CC" "$H/fuzz_armv7.c" "-I$ARMV7" "$B/fuzz/armv7.o"
link fuzz_mips "$CC" "$H/fuzz_mips.c" "-I$MIPS" "$B/fuzz/mips.o"
link fuzz_powerpc "$CC" "$H/fuzz_powerpc.c" "-I$PPC" $(PPC_OBJS "$B/fuzz")
CXX17="-std=c++17"
"$CXX" $CXX17 $FUZZ_FLAGS $COV -I$THUMB -I$ARMV7 -c "$H/fuzz_thumb2.cpp" -o "$B/fuzz_thumb2.o"
"$CXX" $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz_thumb2.o" "$B/fuzz/thumb_disassembler.o" "$B/fuzz/thumb_spec.o" "$B/lsan_off.o" -o "$SRC/fuzz_thumb2"
"$CXX" $FUZZ_FLAGS "$B/standalone_main.o" "$B/fuzz_thumb2.o" "$B/fuzz/thumb_disassembler.o" "$B/fuzz/thumb_spec.o" "$B/lsan_off.o" -o "$SRC/fuzz_thumb2-standalone"

# --- 2b) demangler plugins (demangler/gnu3, demangler/msvc) on a harness-side fake core.
# The closed core is absent: upstream's stubs/generate_stubs.py turns binaryninjacore.h into no-op
# stubs, compiled here as WEAK symbols; mayhem/harnesses/fake_core.cpp overrides the few the
# demangler path needs (demangler registration, platform/arch sizes, string allocation).
# fmt is upstream's vendor/fmt submodule, cached in the image at $FMT_DIR (header-only use).
: "${FMT_DIR:=/opt/toolchains/deps/fmt-11.2.0/src}"
[ -f "$FMT_DIR/include/fmt/format.h" ] || { echo "fmt not found at $FMT_DIR" >&2; exit 1; }
API_CXXF="-std=c++20 -I$SRC -I$FMT_DIR/include -DFMT_HEADER_ONLY"
mkdir -p "$B/stubs"
python3 -I stubs/generate_stubs.py binaryninjacore.h "$B/stubs" >/dev/null
# Weak, and every `char*` getter returns an empty heap string instead of NULL (the C++ API builds
# std::string from those results, which throws on NULL).
python3 -I - "$B/stubs/stubs.cpp" "$B/stubs/stubs_weak.cpp" "$B/stubs/stubs_view.cpp" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
src = re.sub(r'(BINARYNINJACOREAPI char\* [^{;]*?)\{ return \{ \}; \}', r'\1{ return strdup(""); }', src)
src = re.sub(r'(?m)^BINARYNINJACOREAPI ', '__attribute__((weak)) BINARYNINJACOREAPI ', src)
# List getters report their length through a size_t* out-parameter the no-op stubs never write;
# zero every such out-parameter so callers see an empty list rather than an uninitialized count.
def zero_counts(m):
    decl, body = m.group(1), m.group(2)
    outs = re.findall(r'size_t\s*\*\s*(\w+)\s*[,)]', decl)
    if not outs:
        return m.group(0)
    return decl + '{ ' + ' '.join('if (%s) *%s = 0;' % (o, o) for o in outs) + ' ' + body[1:]
src = re.sub(r'(__attribute__\(\(weak\)\) BINARYNINJACOREAPI [^{;]*?)(\{ (?:return \{ \}; )?\})', zero_counts, src)
open(sys.argv[2], 'w').write('#include <string.h>\n' + src)
# Handle variant (every target links it): stubs returning an opaque core handle (BNType*, BNSymbol*, BNPlatform*, ...) return
# a dummy non-NULL handle, as the real core does; the C++ API only passes handles back to the core.
hdr = open('binaryninjacore.h').read()
opaque = set(re.findall(r'typedef struct (BN\w+) \1;', hdr))
# ...except the handles fake_view_core.cpp implements itself (stubs for those return NULL).
opaque -= {'BNDataBuffer', 'BNBinaryReader', 'BNBinaryView', 'BNBinaryWriter'}
def handle(m):
    return m.group(1) + '{ static char h; return reinterpret_cast<%s*>(&h); }' % m.group(2) if m.group(2) in opaque else m.group(0)
view = re.sub(r'(__attribute__\(\(weak\)\) BINARYNINJACOREAPI (BN\w+)\* [^{;]*?)\{ return \{ \}; \}', handle, src)
open(sys.argv[3], 'w').write('#include <string.h>\n' + view)
PY
API_SRCS="$(ls *.cpp json/*.cpp base/*.cpp base/unicode/*.cpp)"
api() {  # api <mode>: the C++ API wrapper library + weak stubs + fake core (glue, not coverage-instrumented)
	local mode="$1" d="$B/$1/api" CF
	if [ "$mode" = fuzz ]; then CF="$FUZZ_FLAGS"; else CF="-O1 $COVERAGE_FLAGS"; fi
	mkdir -p "$d"
	printf '%s\n' $API_SRCS | xargs -P"$MAYHEM_JOBS" -I{} sh -c 'f="$1"; shift; exec "$@" -c "$f" -o "'"$d"'/$(echo "$f" | tr / _).o"' _ {} "$CXX" $CF $API_CXXF
	rm -f "$B/$mode/libbnapi.a"
	ar rcs "$B/$mode/libbnapi.a" "$d"/*.o
	# Every target links the handle-returning stub variant: the real core never hands the API a NULL
	# type/symbol/platform handle, and e.g. Type::NamedType turns a NULL BNType* into a nullptr
	# Ref<Type> that DemangledTypeNode::Finalize then dereferences (a harness artifact, not a bug).
	"$CXX" $CF $API_CXXF -c "$B/stubs/stubs_view.cpp" -o "$B/$mode/stubs_view.o"
	"$CXX" $CF $API_CXXF -c mayhem/harnesses/fake_core.cpp -o "$B/$mode/fake_core.o"
	local P="" ; [ "$mode" = fuzz ] && P="$COV"
	pids=()
	for dm in gnu3 msvc; do
		"$CXX" $CF $P $API_CXXF -Idemangler -Idemangler/$dm -c demangler/$dm/demangle_$dm.cpp -o "$B/$mode/dm_$dm.o" & pids+=($!)
	done
	"$CXX" $CF $P $API_CXXF -Idemangler -c demangler/demangled_type_node.cpp -o "$B/$mode/dm_typenode.o" & pids+=($!)
	"$CXX" $CF $P $API_CXXF -Idemangler -c demangler/demangled_template_simplifier.cpp -o "$B/$mode/dm_simplifier.o" & pids+=($!)
	for p in "${pids[@]}"; do wait "$p"; done
}
api fuzz
api test
"$CXX" $FUZZ_FLAGS $COV $API_CXXF -Imayhem/harnesses -c "$H/fuzz_demangle.cpp" -o "$B/fuzz_demangle.o"
for dm in gnu3 msvc; do
	DOBJS="$B/fuzz/dm_$dm.o $B/fuzz/dm_typenode.o $B/fuzz/dm_simplifier.o $B/fuzz/fake_core.o $B/fuzz/libbnapi.a $B/fuzz/stubs_view.o $B/lsan_off.o -ldl"
	"$CXX" $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz_demangle.o" $DOBJS -o "$SRC/fuzz_demangle_$dm"
	"$CXX" $FUZZ_FLAGS "$B/standalone_main.o" "$B/fuzz_demangle.o" $DOBJS -o "$SRC/fuzz_demangle_$dm-standalone"
done

# --- 2c) BinaryViewType loader plugins (view/elf, view/pe = PE+COFF+TE, view/md1rom)
# on the same weak stubs (handle-returning variant) plus mayhem/harnesses/fake_view_core.cpp: a raw
# parent view over the input, DataBuffer and BinaryReader. One binary per plugin (each defines
# CorePluginInit); mayhem/harnesses/fuzz_view.cpp drives every view type the plugin registers.
"$CXX" $FUZZ_FLAGS $API_CXXF -c "$H/fake_view_core.cpp" -o "$B/fuzz/fake_view_core.o"
"$CXX" $FUZZ_FLAGS $COV $API_CXXF -I$H -c "$H/fuzz_view.cpp" -o "$B/fuzz_view.o"
view_target() {  # [STUBS=stubs_view] view_target <name> <plugin sources...>
	local name="$1" objs="" pids=() o f extra; shift
	for f in "$@"; do
		o="$B/fuzz/view_${name}_$(echo "$f" | tr / _).o"
		extra=""
		# coffview.cpp reads COFFHeader through a BigObj_COFFHeader* cast in IsTypeValidForData (the
		# field read is inside the object); UBSan's object-size check fires on every non-matching input.
		[ "$f" = view/pe/coffview.cpp ] && extra="-fno-sanitize=object-size"
		"$CXX" $FUZZ_FLAGS $extra $COV $API_CXXF -I"$(dirname "$f")" -c "$f" -o "$o" & pids+=($!)
		objs="$objs $o"
	done
	for p in "${pids[@]}"; do wait "$p"; done
	VOBJS="$objs $B/fuzz/fake_view_core.o $B/fuzz/fake_core.o $B/fuzz/libbnapi.a $B/fuzz/$STUBS.o $B/lsan_off.o -ldl"
	"$CXX" $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz_view.o" $VOBJS -o "$SRC/fuzz_view_$name"
	"$CXX" $FUZZ_FLAGS "$B/standalone_main.o" "$B/fuzz_view.o" $VOBJS -o "$SRC/fuzz_view_$name-standalone"
}
# Mach-O (view/macho) is not built: its fat/universal views nest views whose release order the
# fake core does not model (freeObject on an already-released view), i.e. harness artifacts.
STUBS=stubs_view
view_target elf view/elf/elfview.cpp
view_target pe view/pe/peview.cpp view/pe/coffview.cpp view/pe/teview.cpp
view_target md1rom view/md1rom/md1rom.cpp

# --- 3) known-answer drivers for test.sh, normal flags
T=mayhem/tests
"$CC" -O2 $COVERAGE_FLAGS -I$ARMV7 "$T/armv7_kat.c" "$B/test/armv7.o" -o "$B/armv7_kat"
"$CC" -O2 $COVERAGE_FLAGS -I$MIPS "$T/mips_kat.c" "$B/test/mips.o" -o "$B/mips_kat"
"$CC" -O2 $COVERAGE_FLAGS -I$PPC "$T/powerpc_kat.c" $(PPC_OBJS "$B/test") -o "$B/powerpc_kat"
"$CXX" $CXX17 -O2 $COVERAGE_FLAGS -I$THUMB -I$ARMV7 "$T/thumb2_kat.cpp" "$B/test/thumb_disassembler.o" "$B/test/thumb_spec.o" -o "$B/thumb2_kat"
for dm in gnu3 msvc; do
	"$CXX" -O1 $COVERAGE_FLAGS $API_CXXF -Imayhem/harnesses "$T/demangle_kat.cpp" "$B/test/dm_$dm.o" "$B/test/dm_typenode.o" "$B/test/dm_simplifier.o" \
		"$B/test/fake_core.o" "$B/test/libbnapi.a" "$B/test/stubs_view.o" -ldl -o "$B/demangle_${dm}_kat"
done
echo "build.sh: done"
