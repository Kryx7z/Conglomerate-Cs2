#include "../../../hooks/hooks.h"
#include "../../../config/config.h"
#include "../../../utils/memory/seh_diagnostics.h"
#include "../../../utils/debug_console.h"

void __fastcall H::hkRenderFlashbangOverlay(void* a1, void* a2, void* a3, void* a4, void* a5) {
	DebugConsole::once("antiflash.hook.first", "[runtime] RenderFlashBangOverlay detour reached; antiflash=%d", Config::antiflash ? 1 : 0);
	__try
	{
		if (Config::antiflash) return;
		const auto original = RenderFlashBangOverlay.GetOriginal();
		if (!original)
		{
			DebugConsole::rateLimited("antiflash.original.missing", "[runtime] flash overlay original function is null");
			return;
		}
		return original(a1, a2, a3, a4, a5);
	}
	__except (SehDiagnostics::handle("antiflash.original"))
	{
	}
}
