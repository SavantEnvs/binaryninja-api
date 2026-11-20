// Known-answer driver for mayhem/test.sh: "<hex16> [<hex16>]" halfwords (LE order of the stream)
// -> "<size> <mnemonic>" from thumb_decompose/get_thumb_operation_name.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include "spec.h"
#include "disassembler.h"

int main(void)
{
	char line[256];
	while (fgets(line, sizeof(line), stdin)) {
		unsigned a = 0, b = 0;
		if (sscanf(line, "%x %x", &a, &b) < 1)
			continue;
		decomp_request req;
		memset(&req, 0, sizeof(req));
		req.instr_word16 = (uint16_t)a;
		req.instr_word32 = ((uint32_t)a << 16) | (uint16_t)b;
		req.arch = ARCH_ARMv7;
		req.instrSet = INSTRSET_THUMB2;
		decomp_result res;
		memset(&res, 0, sizeof(res));
		if (thumb_decompose(&req, &res) != STATUS_OK)
			printf("??\n");
		else
			printf("%d %s\n", res.instrSize, get_thumb_operation_name(&res).c_str());
	}
	return 0;
}
