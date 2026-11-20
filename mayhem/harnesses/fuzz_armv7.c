// Fuzz the ARMv7 (A32) decoder and formatter (arch/armv7/armv7_disasm/armv7.c).
// Byte 0 selects endianness; the rest is a stream of 32-bit instruction words, each
// decoded and, on success, rendered the way the armv7 arch plugin does.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "armv7.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 5)
		return -1;
	uint32_t bigEndian = data[0] & 1;
	uint32_t address = 0x1000;
	for (size_t off = 1; off + 4 <= size; off += 4, address += 4) {
		uint32_t word;
		if (bigEndian)
			word = ((uint32_t)data[off] << 24) | ((uint32_t)data[off + 1] << 16) | ((uint32_t)data[off + 2] << 8) | data[off + 3];
		else
			word = ((uint32_t)data[off + 3] << 24) | ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 1] << 8) | data[off];
		Instruction instr;
		memset(&instr, 0, sizeof(instr));
		if (armv7_decompose(word, &instr, address, bigEndian) != 0)
			continue;
		char text[512];
		armv7_disassemble(&instr, text, sizeof(text));
		char op[64];
		get_full_operation(op, sizeof(op), &instr);
	}
	return 0;
}
