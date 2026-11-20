// Fuzz the PowerPC / VLE decoder (arch/powerpc/decode/*.c): length detection, 32-bit and VLE
// 16/32-bit decomposition, branch operand rewriting and mnemonic lookup, as arch_ppc.cpp does.
// Bytes 0-1: decode flags (PPC64/AltiVec/VSX/QPX/PS/SPE/VLE/VLE-translate) and endianness.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "decode.h"

static void render(const Instruction *ins)
{
	(void)GetMnemonic(ins);
	OperandsList list;
	memset(&list, 0, sizeof(list));
	list.numOperands = ins->numOperands;
	for (size_t i = 0; i < ins->numOperands && i < 8; i++)
		list.operands[i] = ins->operands[i];
	switch (ins->id) {
	case PPC_ID_BCx: FillBcxOperands(&list, ins); break;
	case PPC_ID_BCCTRx: FillBcctrxOperands(&list, ins); break;
	case PPC_ID_BCLRx: FillBclrxOperands(&list, ins); break;
	case PPC_ID_VLE_SE_BC: FillVle16BcOperands(&list, ins); break;
	case PPC_ID_VLE_E_BCx: FillVle32BcxOperands(&list, ins); break;
	default: break;
	}
	for (size_t i = 0; i < list.numOperands && i < 8; i++) {
		const Operand *op = &list.operands[i];
		(void)OperandClassName(op->cls);
		(void)PowerPCRegisterName(op->reg);
		(void)GetCRBitName(op->crbit);
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 4)
		return -1;
	uint32_t flags = data[0];
	int big = data[1] & 1;
	const uint8_t *p = data + 2;
	size_t len = size - 2;
	uint64_t addr = 0x10000000;
	while (len >= 2) {
		size_t n = GetInstructionLength(p, len, flags);
		if (n == 0 || n > len)
			break;
		Instruction ins;
		memset(&ins, 0, sizeof(ins));
		ins.numBytes = n;
		int ok = 0;
		if (n == 2) {
			uint16_t w = (uint16_t)((p[0] << 8) | p[1]);  /* VLE is always big-endian */
			ok = Decompose16(&ins, w, addr, flags);
		} else {
			uint32_t w;
			if ((flags & DECODE_FLAGS_VLE) || big)
				w = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
			else
				w = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
			ok = Decompose32(&ins, w, addr, flags);
		}
		if (ok)
			render(&ins);
		p += n;
		len -= n;
		addr += n;
	}
	return 0;
}
