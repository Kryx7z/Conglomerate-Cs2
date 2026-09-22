#include "../../../hooks/hooks.h"
#include "../../../config/config.h"
#include "../../../utils/memory/seh_diagnostics.h"

void __fastcall H::hkRenderFlashbangOverlay(void* a1, void* a2, void* a3, void* a4, void* a5) {
	__try
	{
		if (Config::antiflash) return;
		return RenderFlashBangOverlay.GetOriginal()(a1, a2, a3, a4, a5);
	}
	__except (SehDiagnostics::handle("antiflash.original"))
	{
	}
}
