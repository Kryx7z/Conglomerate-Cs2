#pragma once
#include <cstdint>
#include <cstddef>
#include "../../../conglomerate/utils/memory/memorycommon.h"
#include "../../../conglomerate/utils/math/vector/vector.h"
#include "../../../conglomerate/utils/schema/schema.h"
#include "../C_CSWeaponBase/C_CSWeaponBase.h"
#include <cstdint>

class c_aggregate_object_data
{
public:
	char pad_0000[4]; //0x0000
	int count; //0x0004
	char pad_0008[40]; //0x0008
	int index; //0x0030
}; //Size: 0x0034

class c_aggregate_object_array
{
public:
	void* object; //0x0000
	c_aggregate_object_data* data; //0x0008
};

class CMaterial2
{
public:
	virtual const char* GetName() = 0;
	virtual const char* GetShareName() = 0;
};

struct MaterialKeyVar_t
{
	std::uint64_t uKey;
	const char* szName;

	MaterialKeyVar_t(std::uint64_t uKey, const char* szName) :
		uKey(uKey), szName(szName) { }

	MaterialKeyVar_t(const char* szName, bool bShouldFindKey = false) :
		szName(szName)
	{
		uKey = bShouldFindKey ? FindKey(szName) : 0x0;
	}

	std::uint64_t FindKey(const char* szName)
	{
		using fn = std::uint64_t(__fastcall*)(const char*, unsigned int, int);
		static auto find = reinterpret_cast<fn>(M::patternScan("particles", ("48 89 5C 24 ? 57 48 81 EC ? ? ? ? 33 C0 8B DA")));
		if (find == nullptr)
			return 0x0;

		return find(szName, 0x12, 0x31415926);
	}
};

class CObjectInfo
{
	MEM_PAD(0xB0);
	int nId;
};

class CSceneAnimatableObject {
public:
	CBaseHandle Owner() const {
		if (!this)
			return CBaseHandle();

		return *(CBaseHandle*)((std::uintptr_t)this + 0xC0);
	}
};  

struct Color {
	std::uint8_t r = 0U, g = 0U, b = 0U, a = 0U;
};

class CMeshData
{
public:

private:
	MEM_PAD(0x18); // 0x0
public:
	CSceneAnimatableObject* pSceneAnimatableObject; // 0x18
	CMaterial2* pMaterial; // 0x20
	CMaterial2* pMaterial2; // 0x28
	MEM_PAD(0x20); // 0x30
	Color color; // 0x50
	MEM_PAD(0x1C); // 0x54
};

// The chams hook writes pMaterial and color by raw offset, so a silent layout
// change here means memory corruption instead of a failed render. Verify it at
// compile time instead. If these fire, the offsets below are wrong - fix the
// padding, do not delete the asserts.
static_assert(offsetof(CMeshData, pSceneAnimatableObject) == 0x18, "CMeshData::pSceneAnimatableObject moved");
static_assert(offsetof(CMeshData, pMaterial) == 0x20, "CMeshData::pMaterial moved");
static_assert(offsetof(CMeshData, pMaterial2) == 0x28, "CMeshData::pMaterial2 moved");
static_assert(offsetof(CMeshData, color) == 0x50, "CMeshData::color moved");
