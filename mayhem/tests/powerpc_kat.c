// Known-answer driver for mayhem/test.sh: "<flags hex> <hex word, big-endian value>" -> "<mnemonic> <nops>".
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "decode.h"

int main(void)
{
	char line[256];
	while (fgets(line, sizeof(line), stdin)) {
		unsigned f = 0; unsigned long w = 0;
		if (sscanf(line, "%x %lx", &f, &w) < 2)
			continue;
		Instruction ins;
		memset(&ins, 0, sizeof(ins));
		uint8_t bytes[4] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w};
		size_t n = GetInstructionLength(bytes, 4, f);
		ins.numBytes = n;
		int ok = n == 2 ? Decompose16(&ins, (uint16_t)(w >> 16), 0, f) : n == 4 ? Decompose32(&ins, (uint32_t)w, 0, f) : 0;
		if (!ok) { printf("??\n"); continue; }
		printf("%zu %s %zu\n", n, GetMnemonic(&ins), ins.numOperands);
	}
	return 0;
}
