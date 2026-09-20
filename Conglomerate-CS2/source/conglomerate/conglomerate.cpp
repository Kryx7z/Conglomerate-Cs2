#include "conglomerate.h"

#include "utils/module/module.h"

void Conglomerate::init(HWND& window, ID3D11Device* pDevice, ID3D11DeviceContext* pContext, ID3D11RenderTargetView* mainRenderTargetView) {
    modules.init();

    renderer.menu.init(window, pDevice, pContext, mainRenderTargetView);

    schema.init("client.dll", 0);
    interfaces.init();
    renderer.visuals.init();

    __try
    {
        materials.init();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    hooks.init();
}
