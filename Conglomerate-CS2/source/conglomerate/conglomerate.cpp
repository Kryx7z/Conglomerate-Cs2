#include "conglomerate.h"

#include "utils/module/module.h"
#include "utils/debug_console.h"

void Conglomerate::init(HWND& window, ID3D11Device* pDevice, ID3D11DeviceContext* pContext, ID3D11RenderTargetView* mainRenderTargetView) {
    modules.init();
    DebugConsole::logf("[init] modules: client=%p scenesystem=%p engine2=%p",
        reinterpret_cast<void*>(modules.getModule("client")),
        reinterpret_cast<void*>(modules.getModule("scenesystem")),
        reinterpret_cast<void*>(modules.getModule("engine2")));

    renderer.menu.init(window, pDevice, pContext, mainRenderTargetView);
    DebugConsole::logf("[init] ImGui renderer initialized");

    const bool schemaReady = schema.init("client.dll", 0);
    DebugConsole::logf("[init] schema %s; system=%p", schemaReady ? "ready" : "FAILED",
        static_cast<void*>(schema.schema_system));
    const bool interfacesReady = interfaces.init();
    DebugConsole::logf("[init] interfaces %s; engine=%p entity_service=%p input=%p scene=%p material_system=%p create_material=%p",
        interfacesReady ? "ready" : "PARTIAL/FAILED",
        static_cast<void*>(I::EngineClient), static_cast<void*>(I::GameEntity), I::Input,
        static_cast<void*>(I::SceneSystem), I::MaterialSystem2,
        reinterpret_cast<void*>(I::CreateMaterial));
    renderer.visuals.init();
    DebugConsole::logf("[init] visuals initialized");

    const bool materialsReady = materials.init();
    DebugConsole::logf("[init] chams materials %s", materialsReady ? "ready" : "FAILED (some materials are null)");

    hooks.init();
    DebugConsole::logf("[init] hook registration pass completed");
}
