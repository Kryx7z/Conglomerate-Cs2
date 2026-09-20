#pragma once
#include <memory>

#include "..\fnv1a\fnv1a.h"
#include "..\memory\vfunc\vfunc.h"
#include "..\math\utlmemory\utlmemory.h"
#include "..\math\utlvector\utlvector.h"
#include "..\..\..\cs2\datatypes\schema\ISchemaClass\ISchemaClass.h"

#define SCHEMA_ADD_OFFSET(TYPE, NAME, OFFSET)                                                                 \
	[[nodiscard]] inline std::add_lvalue_reference_t<TYPE> NAME()                                          \
	{                                                                                                         \
		static const std::uint32_t uOffset = OFFSET;                                                          \
		return *reinterpret_cast<std::add_pointer_t<TYPE>>(reinterpret_cast<std::uint8_t*>(this) + (uOffset)); \
	}

#define SCHEMA_ADD_POFFSET(TYPE, NAME, OFFSET)                                                               \
	[[nodiscard]] inline std::add_pointer_t<TYPE> NAME()                                                  \
	{                                                                                                        \
		const static std::uint32_t uOffset = OFFSET;                                                         \
		return reinterpret_cast<std::add_pointer_t<TYPE>>(reinterpret_cast<std::uint8_t*>(this) + (uOffset)); \
	}


#define schema(TYPE, NAME, FIELD)  SCHEMA_ADD_OFFSET(TYPE, NAME, SchemaFinder::Get(FIELD) + 0u)

#define schema_pfield(TYPE, NAME, FIELD, ADDITIONAL) SCHEMA_ADD_POFFSET(TYPE, NAME, SchemaFinder::Get(FIELD) + ADDITIONAL)

class Schema {
public:
	bool init( const char* module_name, int module_type);

	ISchemaSystem* schema_system = nullptr;

};

// "ClassName->FieldName" -> offset, resolved lazily through the game's own
// CSchemaSystemTypeScope::FindDeclaredClass (real vtable call), so we never
// need to guess the internal layout of CSchemaSystemTypeScope ourselves.
namespace SchemaFinder {

	[[nodiscard]] std::uint32_t Get(const char* fullPath);
	[[nodiscard]] std::uint32_t GetExternal(const char* moduleName, const char* className, const char* fieldName);
}