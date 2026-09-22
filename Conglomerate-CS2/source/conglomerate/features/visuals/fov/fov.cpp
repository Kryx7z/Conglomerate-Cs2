#include <algorithm>
#include "../../../hooks/hooks.h"
#include "../../../config/config.h"
#include "../../../interfaces/interfaces.h"
#include "../../../utils/memory/safe_memory.h"

static float hkGetRenderFov_impl(void* rcx)
{
	const auto original = H::GetRenderFov.GetOriginal();
	if (!Config::fovEnabled)
	{
		H::g_flActiveFov = original ? original(rcx) : 90.0f;
		return H::g_flActiveFov;
	}

	const float requestedFov = std::clamp(Config::fov, 20.0f, 160.0f);
	// Never transform the game's scoped FOV. AWP/SSG use the same original
	// camera path for both zoom levels, so replacing or scaling it breaks the
	// second zoom step.
	bool scoped = false;
	if (H::oGetLocalPlayer)
	{
		if (auto* localPawn = H::oGetLocalPlayer(0))
			scoped = localPawn->m_bIsScoped();
	}

	const float originalFov = original ? original(rcx) : 90.0f;
	if (scoped)
		H::g_flActiveFov = originalFov;
	else
		H::g_flActiveFov = requestedFov;

	return H::g_flActiveFov;
}

float H::hkGetRenderFov(void* rcx) {
	__try
	{
		return hkGetRenderFov_impl(rcx);
	}
	__except (SehDiagnostics::handle("fov.render"))
	{
		// A weapon/schema read must never turn the custom FOV off. If the
		// scoped-state path is unavailable for a frame, keep the requested FOV.
		return Config::fovEnabled ? std::clamp(Config::fov, 20.0f, 160.0f) : 90.0f;
	}
}

static bool ReadLocalScopedState()
{
	__try
	{
		if (H::oGetLocalPlayer)
		{
			if (auto* localPawn = H::oGetLocalPlayer(0))
				return localPawn->m_bIsScoped();
		}
		return false;
	}
	__except (SehDiagnostics::handle("fov.scoped_state"))
	{
		return false;
	}
}

static bool WriteViewFov(void* viewSetup, float value)
{
	if (!viewSetup)
		return false;

	return SafeMemory::write(reinterpret_cast<std::uintptr_t>(viewSetup) + 0x498u, value);
}

static void ApplyOverrideViewFov(void* viewSetup)
{
	if (!Config::fovEnabled || !viewSetup || ReadLocalScopedState())
		return;

	// The view-setup path is reached for every weapon, including AUG and SG.
	WriteViewFov(viewSetup, std::clamp(Config::fov, 20.0f, 160.0f));
}

void __fastcall H::hkOverrideView(void* self, void* viewSetup)
{
	static auto original = H::OverrideView.GetOriginal();
	if (original)
		original(self, viewSetup);

	ApplyOverrideViewFov(viewSetup);
}
