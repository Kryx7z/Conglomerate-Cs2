#include "..\..\..\conglomerate\hooks\hooks.h"
#include "..\..\..\conglomerate\interfaces\interfaces.h"

#include "cutlbuffer.h"

CUtlBuffer::CUtlBuffer(int a1, int size, int a3)
{
	if (I::ConstructUtlBuffer)
		I::ConstructUtlBuffer(this, a1, size, a3);
}

void CUtlBuffer::ensure(int size)
{
	if (I::EnsureCapacityBuffer)
		I::EnsureCapacityBuffer(this, size);
}

void CUtlBuffer::PutString(const char* szString)
{
	if (I::PutUtlString)
		I::PutUtlString(this, szString);
}
