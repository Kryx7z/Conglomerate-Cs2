#pragma once
#include <Windows.h>
#include <d3d11.h>
#include "../../../external/imgui/imgui.h"
#include "../../../external/imgui/imgui_impl_dx11.h"
#include "../../../external/imgui/imgui_impl_win32.h"

namespace UiState
{
	inline bool menuOpen = false;
}

class Menu {
public:
	Menu();

	void init(HWND& window, ID3D11Device* pDevice, ID3D11DeviceContext* pContext, ID3D11RenderTargetView* mainRenderTargetView);
	void render();

	void toggleMenu();
	[[nodiscard]] bool isOpen() const { return showMenu; }
private:
	bool showMenu;
	bool savedRelativeMouse = false;
	bool relativeMouseStateCaptured = false;
	int activeTab;
};
