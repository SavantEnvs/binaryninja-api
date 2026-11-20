// Fuzz the generated Thumb/Thumb2 decoder (arch/armv7/thumb2_disasm/disassembler.cpp + spec.cpp).
// Byte 0: bit0 big-endian, bit1/2 if-then state, bit3 carry-in; byte 1 selects the instruction set.
// The rest is the instruction stream, decoded the way arch_thumb2.cpp's populateDecomposeRequest does.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include "spec.h"
#include "disassembler.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 4)
		return -1;
	bool big = data[0] & 1;
	static const uint8_t ifthen[4] = {IFTHEN_UNKNOWN, IFTHEN_NO, IFTHEN_YES, IFTHEN_UNKNOWN};
	static const uint8_t ifthenlast[4] = {IFTHENLAST_UNKNOWN, IFTHENLAST_NO, IFTHENLAST_YES, IFTHENLAST_UNKNOWN};
	uint8_t itsel = (data[0] >> 1) & 3;
	uint8_t carry = (data[0] >> 3) & 1;
	uint8_t instrSet = data[1] % 3;
	const uint8_t *p = data + 2;
	size_t len = size - 2;
	uint32_t addr = 0x1000;
	while (len >= 2) {
		decomp_request req;
		memset(&req, 0, sizeof(req));
		if (!big) {
			uint16_t w0 = p[0] | (p[1] << 8);
			req.instr_word16 = w0;
			if (len >= 4)
				req.instr_word32 = ((uint32_t)w0 << 16) | (uint16_t)(p[2] | (p[3] << 8));
		} else {
			req.instr_word16 = (p[0] << 8) | p[1];
			if (len >= 4)
				req.instr_word32 = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
		}
		req.arch = ARCH_ARMv7;
		req.instrSet = instrSet;
		req.inIfThen = ifthen[itsel];
		req.inIfThenLast = ifthenlast[itsel];
		req.carry_in = carry;
		req.addr = addr;
		decomp_result res;
		memset(&res, 0, sizeof(res));
		size_t step = 2;
		if (thumb_decompose(&req, &res) == STATUS_OK) {
			std::string name = get_thumb_operation_name(&res);
			(void)thumb_has_writeback(&res);
			(void)get_thumb_condition_name(res.fields[FIELD_cond] & 0xf);
			if (res.instrSize == 32)
				step = 4;
		}
		if (step > len)
			break;
		p += step;
		len -= step;
		addr += step;
	}
	return 0;
}
