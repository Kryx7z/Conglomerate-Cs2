#include "gaa.h"

uintptr_t M::getAbsoluteAddress(uintptr_t addr, const int nPreOffset, const int nPostOffset) {
	if (addr == 0)
		return 0;

	addr += nPreOffset;
	int32_t nRva = *reinterpret_cast<int32_t*>(addr);
	addr += nPostOffset + sizeof(uint32_t) + nRva;
	return addr;
}