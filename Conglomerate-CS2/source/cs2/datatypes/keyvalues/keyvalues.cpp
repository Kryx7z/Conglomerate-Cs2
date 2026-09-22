#pragma once
#include <cstdint>
#include <cstring>
#include <climits>
#include <new>
#include <Windows.h>
#include "../cutlbuffer/cutlbuffer.h"
#include "keyvalues.h"
#include "../../../conglomerate/utils/memory/vfunc/vfunc.h"
#include "../../../conglomerate/utils/memory/patternscan/patternscan.h"
#include "../../../conglomerate/utils/memory/memorycommon.h"
#include "../../../conglomerate/utils/memory/seh_diagnostics.h"
#include "../cutl/utlhash/utlhash.h"
#include "../../../conglomerate/utils/math/utlvector/utlvector.h"
#include "../../../conglomerate/interfaces/interfaces.h"


bool CKeyValues3::LoadFromBuffer(const char* szString)
{
    if (!szString)
        return false;

    KV3ID_t kv3ID(
        "generic",
        0x41B818518343427E,
        0xB5F447C23C0CDF8C
    );

    if (I::LoadKV3TextExport)
    {
        __try
        {
            const bool loaded = I::LoadKV3TextExport(
                this,
                nullptr,
                szString,
                &kv3ID,
                nullptr,
                0U
            );
            return loaded;
        }
        __except (SehDiagnostics::handle("keyvalues.load_text"))
        {
            return false;
        }
    }

    // These are resolved from client.dll first and tier0.dll second. A build
    // can change the decorated constructor name, so the interface initializer
    // tries both the current BufferFlags_t overload and the legacy overload.
    if (!I::ConstructUtlBuffer || !I::PutUtlString)
    {
        return false;
    }

    // CUtlBuffer takes an int size; reject inputs that cannot be represented
    // instead of silently truncating the allocation size.
    const size_t stringLength = std::strlen(szString);
    if (stringLength > static_cast<size_t>(INT_MAX) - 10)
    {
        return false;
    }

    const int bufferSize = static_cast<int>(stringLength + 10);
    CUtlBuffer buffer(0, bufferSize, 1);

    buffer.PutString(szString);
    const bool loaded = this->LoadKV3(&buffer);
    return loaded;
}


using fnLoadKeyValues = bool(__fastcall*)(
    CKeyValues3*,
    void*,
    CUtlBuffer*,
    KV3ID_t*,
    void*,
    void*,
    void*,
    void*,
    const char*
);


static bool SafeCallLoadKV3(
    fnLoadKeyValues fn,
    CKeyValues3* keyValues,
    CUtlBuffer* buffer,
    KV3ID_t* kv3ID
)
{
    __try
    {
        return fn(
            keyValues,
            nullptr,
            buffer,
            kv3ID,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            ""
        );
    }
    __except (SehDiagnostics::handle("keyvalues.load_internal"))
    {
        return false;
    }
}

static bool SafeCallLoadKV3Export(
    CKeyValues3* keyValues,
    CUtlBuffer* buffer,
    const KV3ID_t& kv3ID
)
{
    __try
    {
        return I::LoadKV3Export(keyValues, nullptr, buffer, kv3ID, "", 0);
    }
    __except (SehDiagnostics::handle("keyvalues.load_export"))
    {
        return false;
    }
}


bool CKeyValues3::LoadKV3(CUtlBuffer* buffer)
{
    if (!buffer)
        return false;

    KV3ID_t kv3ID(
        "generic",
        0x41B818518343427E,
        0xB5F447C23C0CDF8C
    );

    // The exported six-argument loader is present in current builds, but it
    // is not the loader used by materialsystem2's CreateMaterial path.  The
    // internal loader below fills the material-resource representation that
    // CreateMaterial consumes.  Prefer it whenever its call target is found.
    static const fnLoadKeyValues oLoadKeyValues =
        reinterpret_cast<fnLoadKeyValues>(
            M::abs(
                M::FindPattern(
                    "tier0.dll",
                    "E8 ? ? ? ? EB ? F7 43"  // Kryx7z-verified pattern
                ),
                0x1,
                0x0
            )
        );

    if (oLoadKeyValues)
    {
        return SafeCallLoadKV3(
            oLoadKeyValues,
            this,
            buffer,
            &kv3ID
        );
    }

    // Keep the exported overload as a fallback for builds where Valve removes
    // or relocates the internal call target.
    if (I::LoadKV3Export)
    {
        return SafeCallLoadKV3Export(this, buffer, kv3ID);
    }

    if (!oLoadKeyValues && !I::LoadKV3Export)
    {
        return false;
    }

    return false;
}


static CKeyValues3* SafeCallSetTypeKV3(
    CKeyValues3*(__fastcall* fn)(CKeyValues3*, unsigned int, unsigned int),
    CKeyValues3* pKeyValue
)
{
    __try
    {
        return fn(pKeyValue, 1U, 6U);
    }
    __except (SehDiagnostics::handle("keyvalues.set_type"))
    {
        CKeyValues3::destroy_material_resource(pKeyValue);
        return nullptr;
    }
}


using fnSetTypeKV3 =
    CKeyValues3*(__fastcall*)(CKeyValues3*, unsigned int, unsigned int);


// Only the shape of SetTypeKV3's prologue is stable: the stack size, the type
// id it compares against and the id it remaps to are all immediates, and those
// are exactly what moves when the game is rebuilt. Pinning them (as the fully
// spelled out signature from the public dumps does) is what makes the scan
// stop matching after a patch.
static const char* const kSetTypeKV3Patterns[] =
{
    "40 53 48 83 EC ? 80 FA ? 0F B6 C2 41 B9 ? ? ? ? 48 8B D9 44 0F 45 C8",
    "40 53 48 83 EC 30 80 FA 06 0F B6 C2 41 B9 16 00 00 00 48 8B D9 44 0F 45 C8",
    "40 53 48 83 EC ? 80 FA",
};

// KeyValues3 is a tier1 type, so don't hard fail just because this particular
// build keeps SetTypeKV3 somewhere other than client.dll.
static const char* const kSetTypeKV3Modules[] =
{
    "client.dll",
    "tier0.dll",
    "materialsystem2.dll",
};


static bool is_executable_address(const void* address)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;

    if (mbi.State != MEM_COMMIT)
        return false;

    switch (mbi.Protect & 0xFFu)
    {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}


// This is the current CS2 client build's SetTypeKV3 RVA.  It is deliberately
// a guarded fallback rather than an unconditional offset: an updated binary
// must still contain the complete known prologue before it can be called.
static fnSetTypeKV3 resolve_set_type_kv3_current_build_fallback()
{
    constexpr std::uintptr_t kSetTypeKV3Rva = 0x1885AD0;
    // The bytes marked -1 are build-specific immediates (the KV3 type IDs).
    // Keep the surrounding instruction shape strict while allowing those IDs
    // to change between ordinary client updates.
    static constexpr int kExpectedPrologue[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, -1, 0x80, 0xFA, -1,
        0x0F, 0xB6, 0xC2, 0x41, 0xB9, -1, -1, -1, -1,
        0x48, 0x8B, 0xD9, 0x44, 0x0F, 0x45, 0xC8
    };

    const HMODULE client = GetModuleHandleA("client.dll");
    if (!client)
    {
        return nullptr;
    }

    auto* address = reinterpret_cast<std::uint8_t*>(client) + kSetTypeKV3Rva;
    if (!is_executable_address(address))
    {
        return nullptr;
    }

    bool prologueMatches = true;
    for (std::size_t i = 0; i < std::size(kExpectedPrologue); ++i)
    {
        if (kExpectedPrologue[i] >= 0 &&
            address[i] != static_cast<std::uint8_t>(kExpectedPrologue[i]))
        {
            prologueMatches = false;
            break;
        }
    }

    if (!prologueMatches)
    {
        return nullptr;
    }

    return reinterpret_cast<fnSetTypeKV3>(address);
}


static fnSetTypeKV3 resolve_set_type_kv3()
{
    for (const char* module : kSetTypeKV3Modules)
    {
        if (!GetModuleHandleA(module))
            continue;

        for (const char* pattern : kSetTypeKV3Patterns)
        {
            std::uint8_t* address = M::FindPattern(module, pattern);
            if (!address)
                continue;

            // A byte sequence match is not proof of a function. Never hand
            // back an address that is about to be called through without
            // checking that it is live, executable code first.
            if (!is_executable_address(address))
            {
                continue;
            }

            return reinterpret_cast<fnSetTypeKV3>(address);
        }
    }

    return resolve_set_type_kv3_current_build_fallback();
}


CKeyValues3* CKeyValues3::create_material_from_resource()
{
    // Do not permanently cache a failed lookup.  On some launches client.dll
    // is attached a moment after the first interfaces become available.
    static fnSetTypeKV3 oSetTypeKV3 = nullptr;
    if (!oSetTypeKV3)
        oSetTypeKV3 = resolve_set_type_kv3();

    if (!oSetTypeKV3)
    {
        return nullptr;
    }

    void* storage = ::operator new(0x200, std::nothrow);
    if (!storage)
        return nullptr;

    std::memset(storage, 0, 0x200);
    CKeyValues3* pKeyValue = reinterpret_cast<CKeyValues3*>(storage);

    CKeyValues3* result = SafeCallSetTypeKV3(oSetTypeKV3, pKeyValue);

    if (!result || IsBadReadPtr(result, sizeof(void*))) {
        destroy_material_resource(pKeyValue);
        return nullptr;
    }

    return result;
}


void CKeyValues3::destroy_material_resource(CKeyValues3* keyValues)
{
    if (keyValues)
        ::operator delete(static_cast<void*>(keyValues));
}
