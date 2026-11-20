// Known-answer driver for mayhem/test.sh: reads "<hex insword> <addr>" lines on stdin and prints
// the armv7 disassembly of each (the same calls upstream's armv7_disasm/test.py makes via ctypes).
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "armv7.h"

int main(void)
{
	char line[256];
	while (fgets(line, sizeof(line), stdin)) {
		unsigned long w = 0, a = 0;
		if (sscanf(line, "%lx %lx", &w, &a) < 1)
			continue;
		Instruction instr;
		memset(&instr, 0, sizeof(instr));
		char text[2048];
		memset(text, 0, sizeof(text));
		uint32_t rc = armv7_decompose((uint32_t)w, &instr, (uint32_t)a, 0);
		if (rc == 1) strcpy(text, "decomposer failed");
		else if (rc == 2) strcpy(text, "group decomposition failed");
		else if (rc == 3) strcpy(text, "unimplemented");
		else if (rc == 4) strcpy(text, "disassembler failed");
		else if (armv7_disassemble(&instr, text, sizeof(text)) != 0) strcpy(text, "disassembly failed");
		printf("%s\n", text);
	}
	return 0;
}
