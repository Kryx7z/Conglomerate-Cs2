#include "C_CSPlayerPawn.h"

#include "../../../conglomerate/offsets/offsets.h"
#include "../../../conglomerate/interfaces/interfaces.h"

C_CSPlayerPawn::C_CSPlayerPawn(uintptr_t address) : address(address) {}

Vector_t C_CSPlayerPawn::getPosition() const {
	// Must read through `this`, not the ctor's `address` member: entities come
	// out of the entity system as reinterpret_cast<C_CSPlayerPawn*>, so
	// `address` holds whatever game data happens to sit at that offset.
	const std::uint32_t offset = SchemaFinder::Get("C_BasePlayerPawn->m_vOldOrigin");
	if (!this || offset == 0U)
		return Vector_t();

	return *reinterpret_cast<Vector_t*>(reinterpret_cast<uintptr_t>(this) + offset);
}

Vector_t C_CSPlayerPawn::getEyePosition() const {
	return Vector_t();
}

C_CSWeaponBase* C_CSPlayerPawn::GetActiveWeapon() const {
	if (!this)
		return nullptr;

	CCSPlayer_WeaponServices* weapon_services = this->GetWeaponServices();
	if (weapon_services == nullptr)
		return nullptr;

	if (!I::GameEntity || !I::GameEntity->Instance)
		return nullptr;

	const auto active_handle = weapon_services->m_hActiveWeapon();
	if (!active_handle.valid())
		return nullptr;

	C_CSWeaponBase* active_weapon = I::GameEntity->Instance->Get<C_CSWeaponBase>(active_handle.index());
	if (!active_weapon)
		return nullptr;

	return active_weapon;
}

CCSPlayer_WeaponServices* C_CSPlayerPawn::GetWeaponServices() const {
	const std::uint32_t offset = SchemaFinder::Get("C_BasePlayerPawn->m_pWeaponServices");
	if (!this || offset == 0U)
		return nullptr;

	return reinterpret_cast<CCSPlayer_WeaponServices*>(reinterpret_cast<uintptr_t>(this) + offset);
}

uintptr_t C_CSPlayerPawn::getAddress() const {
	return address;
}

int C_CSPlayerPawn::getHealth() const {
	const std::uint32_t offset = SchemaFinder::Get("C_BaseEntity->m_iHealth");
	if (!this || offset == 0U)
		return 0;

	return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + offset);
}

uint8_t C_CSPlayerPawn::getTeam() const {
	const std::uint32_t offset = SchemaFinder::Get("C_BaseEntity->m_iTeamNum");
	if (!this || offset == 0U)
		return 0;

	return *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(this) + offset);
}

Vector_t C_CSPlayerPawn::getViewOffset() const {
	const std::uint32_t offset = SchemaFinder::Get("C_BaseModelEntity->m_vecViewOffset");
	if (!this || offset == 0U)
		return Vector_t();

	return *reinterpret_cast<Vector_t*>(reinterpret_cast<uintptr_t>(this) + offset);
}
