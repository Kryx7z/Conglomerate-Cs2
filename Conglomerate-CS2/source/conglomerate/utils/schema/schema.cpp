#include "schema.h"
#include <string>
#include <unordered_map>
#include <Shlobj.h>
#include <shlobj_core.h>

#include "../fnv1a/fnv1a.h"
#include "../math/utlstring/utlstring.h"
#include "../memory/Interface/Interface.h"

// Cache of resolved "ClassName->FieldName" -> offset, keyed by the fnv1a hash
// of the full path string, so repeated lookups (SCHEMA_ADD_OFFSET caches per
// call site too, but this covers lookups from different call sites sharing
// the same field) don't re-walk the schema class field list every time.
static std::unordered_map<uint32_t, std::uint32_t> resolved_offsets;

// Per-module type scopes, resolved on demand. Most fields live in "client.dll",
// which is what Schema::init() sets up as the default scope below.
static std::unordered_map<std::string, CSchemaSystemTypeScope*> type_scopes;

static CSchemaSystemTypeScope* default_scope = nullptr;
static ISchemaSystem* g_schema_system = nullptr;

static CSchemaSystemTypeScope* GetTypeScope(const char* moduleName)
{
	const auto it = type_scopes.find(moduleName);
	if (it != type_scopes.end())
		return it->second;

	if (!g_schema_system)
		return nullptr;

	CSchemaSystemTypeScope* pScope = g_schema_system->FindTypeScopeForModule(moduleName);
	type_scopes[moduleName] = pScope; // cache even if nullptr, avoids retrying every call
	return pScope;
}

// Resolves "ClassName->FieldName" against a given type scope by asking the
// game's own CSchemaSystemTypeScope::FindDeclaredClass (real vtable call into
// client.dll) for the class, then linearly scanning its field list. This
// deliberately avoids poking at CSchemaSystemTypeScope's internal hash table
// (hashClasses) directly, since that struct's internal padding/offsets are
// not something we can reliably determine without a live debugger, and
// getting it wrong causes exactly the kind of crash we were chasing.
//
// If the field isn't declared directly on the requested class, walks up
// pBaseClasses recursively - schema reflection only lists a class's own
// fields, not inherited ones, so a field like C_BasePlayerPawn::m_pWeaponServices
// won't show up when asked for under a derived class like C_CSPlayerPawn
// unless we search parents too. Returns the offset already adjusted by the
// base class's nOffset within the derived class, so callers can keep using
// it directly regardless of which class actually declares the field.
static std::uint32_t ResolveFieldInClass(SchemaClassInfoData_t* pClassInfo, const std::string& fieldName, int depth = 0)
{
	if (!pClassInfo || depth > 8) // guard against unexpected cycles
		return 0U;

	if (pClassInfo->nFieldSize > 0 && pClassInfo->pFields)
	{
		for (int i = 0; i < pClassInfo->nFieldSize; i++)
		{
			SchemaClassFieldData_t& field = pClassInfo->pFields[i];
			if (field.szName && fieldName == field.szName)
				return field.nSingleInheritanceOffset;
		}
	}

	if (pClassInfo->nBaseClassesCount > 0 && pClassInfo->pBaseClasses)
	{
		for (int i = 0; i < pClassInfo->nBaseClassesCount; i++)
		{
			SchemaBaseClassInfoData_t& base = pClassInfo->pBaseClasses[i];
			if (!base.pClass)
				continue;

			std::uint32_t offset = ResolveFieldInClass(base.pClass, fieldName, depth + 1);
			if (offset != 0U)
				return static_cast<std::uint32_t>(base.nOffset) + offset;
		}
	}

	return 0U;
}

static std::uint32_t ResolveField(CSchemaSystemTypeScope* pTypeScope, const std::string& className, const std::string& fieldName)
{
	if (!pTypeScope)
		return 0U;

	SchemaClassInfoData_t* pClassInfo = nullptr;
	pTypeScope->FindDeclaredClass(&pClassInfo, className.c_str());
	if (!pClassInfo)
		return 0U;

	return ResolveFieldInClass(pClassInfo, fieldName);
}

bool Schema::init(const char* ModuleName, int module_type)
{
	schema_system = I::Get<ISchemaSystem>("schemasystem.dll", "SchemaSystem_001");
	if (!schema_system)
		return false;

	g_schema_system = schema_system;

	default_scope = schema_system->FindTypeScopeForModule(ModuleName);
	if (default_scope == nullptr)
		return false;

	type_scopes[ModuleName] = default_scope;

	return true;
}

std::uint32_t SchemaFinder::Get(const char* fullPath)
{
	const uint32_t hashedName = hash_32_fnv1a_const(fullPath);

	if (const auto it = resolved_offsets.find(hashedName); it != resolved_offsets.end())
		return it->second;

	const std::string path(fullPath);
	const auto arrow = path.find("->");
	if (arrow == std::string::npos)
		return 0U;

	const std::string className = path.substr(0, arrow);
	const std::string fieldName = path.substr(arrow + 2);

	const std::uint32_t offset = ResolveField(default_scope, className, fieldName);

	resolved_offsets[hashedName] = offset;
	return offset;
}

std::uint32_t SchemaFinder::GetExternal(const char* moduleName, const char* className, const char* fieldName)
{
	const std::string key = std::string(moduleName) + "|" + className + "->" + fieldName;
	const uint32_t hashedName = hash_32_fnv1a_const(key.c_str());

	if (const auto it = resolved_offsets.find(hashedName); it != resolved_offsets.end())
		return it->second;

	CSchemaSystemTypeScope* pScope = GetTypeScope(moduleName);
	const std::uint32_t offset = ResolveField(pScope, className, fieldName);

	resolved_offsets[hashedName] = offset;
	return offset;
}
