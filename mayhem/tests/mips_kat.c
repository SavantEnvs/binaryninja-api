// Known-answer driver for mayhem/test.sh: "<version> <hex insword>" per line -> MIPS disassembly.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "mips.h"

int main(void)
{
	char line[256];
	while (fgets(line, sizeof(line), stdin)) {
		int v = 0; unsigned long w = 0;
		if (sscanf(line, "%d %lx", &v, &w) < 2)
			continue;
		uint32_t word = (uint32_t)w;
		Instruction instr;
		memset(&instr, 0, sizeof(instr));
		char text[512] = "??";
		if (mips_decompose(&word, 4, &instr, (MipsVersion)v, 0, 0, 1) != 0 || mips_disassemble(&instr, text, sizeof(text)) != 0)
			strcpy(text, "??");
		printf("%s\n", text);
	}
	return 0;
}
