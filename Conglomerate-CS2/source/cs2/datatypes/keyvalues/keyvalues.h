#pragma once
#include <cstdint>
#include "../../../conglomerate/utils/memory/vfunc/vfunc.h"
#include "../../../conglomerate/utils/memory/patternscan/patternscan.h"
#include "../../../conglomerate/utils/memory/memorycommon.h"
#include "../cutl/utlhash/utlhash.h"
#include "..\..\..\conglomerate\utils\math\utlvector\utlvector.h"
#include "..\cutlbuffer\cutlbuffer.h"
struct KV3ID_t
{
	const char* szName;
	std::uint64_t unk0;
	std::uint64_t unk1;
};

class CKeyValues3
{
public:
	char _pad[0x100];
	std::uint64_t uKey;
	void* pValue;
	char _pad1[0x8];

	bool LoadFromBuffer(const char* szString);
	bool LoadKV3(CUtlBuffer* buffer);
	void LoadFromFile(const char* szString);

	static CKeyValues3* create_material_from_resource();
	static void destroy_material_resource(CKeyValues3* keyValues);
};
