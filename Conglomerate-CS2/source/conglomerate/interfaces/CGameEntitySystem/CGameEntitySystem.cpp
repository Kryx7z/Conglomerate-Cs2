#include "CGameEntitySystem.h"
#include "..\..\hooks\hooks.h"

void* CGameEntitySystem::GetEntityByIndex(int nIndex) {
	if (!H::ogGetBaseEntity)
		return nullptr;

	return H::ogGetBaseEntity(this, nIndex);
}
