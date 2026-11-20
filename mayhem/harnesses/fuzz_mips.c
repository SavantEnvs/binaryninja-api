// Fuzz the MIPS decoder and formatter (arch/mips/mips/mips.c).
// Byte 0 picks the MIPS version, byte 1 flags (bit0 big-endian, bit1 pseudo-ops); the rest
// is the instruction stream handed to mips_decompose (up to 8 bytes per call, as the plugin does).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mips.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 6)
		return -1;
	static const MipsVersion versions[] = {MIPS_1, MIPS_2, MIPS_3, MIPS_4, MIPS_32, MIPS_64, MIPS_R5900};
	MipsVersion version = versions[data[0] % (sizeof(versions) / sizeof(versions[0]))];
	uint32_t big = data[1] & 1;
	uint32_t pseudo = (data[1] >> 1) & 1;
	uint64_t address = 0x400000;
	for (size_t off = 2; off + 4 <= size; off += 4, address += 4) {
		uint32_t words[2] = {0, 0};
		size_t avail = size - off >= 8 ? 8 : 4;
		memcpy(words, data + off, avail);
		Instruction instr;
		memset(&instr, 0, sizeof(instr));
		if (mips_decompose(words, avail, &instr, version, address, big, pseudo) != 0)
			continue;
		char text[512];
		mips_disassemble(&instr, text, sizeof(text));
	}
	return 0;
}
