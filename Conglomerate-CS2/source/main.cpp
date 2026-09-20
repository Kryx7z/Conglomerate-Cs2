#include "includes.h"
#include "conglomerate/conglomerate.h"
#include "conglomerate/renderer/icons.h"

#include "../external/kiero/minhook/include/MinHook.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Conglomerate conglomerate;

Present oPresent;
HWND window = NULL;
WNDPROC oWndProc;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView;

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT && (lParam & (1LL << 30)) == 0) {
        conglomerate.renderer.menu.toggleMenu();
        return 0;
    }

    if (uMsg == WM_KEYUP && wParam == VK_INSERT)
        return 0;

    if (conglomerate.renderer.menu.isOpen()) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
            return true;
        }

        switch (uMsg) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_INPUT: // raw input - this is how most Source2 games actually read camera look
            // Swallow it: don't forward to the game's own WndProc, so it never
            // sees mouse movement/clicks or keyboard input while our menu is open.
            return 0;
        default:
            break;
        }
    }

    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

bool init = false;

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    if (!init)
    {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)))
        {
            pDevice->GetImmediateContext(&pContext);
            if (pContext == nullptr)
            {
                pDevice->Release();
                pDevice = nullptr;
                return oPresent(pSwapChain, SyncInterval, Flags);
            }

            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            window = sd.OutputWindow;

            // pBackBuffer used to be left uninitialised, so a failed GetBuffer
            // handed a garbage pointer to CreateRenderTargetView.
            ID3D11Texture2D* pBackBuffer = nullptr;
            if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<LPVOID*>(&pBackBuffer))) || pBackBuffer == nullptr)
                return oPresent(pSwapChain, SyncInterval, Flags);

            const HRESULT rtvResult = pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
            pBackBuffer->Release();

            if (FAILED(rtvResult) || mainRenderTargetView == nullptr)
                return oPresent(pSwapChain, SyncInterval, Flags);

            oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
            // One bad pattern scan inside init() must not take the whole game
            // down with it.
            __try
            {
                conglomerate.init(window, pDevice, pContext, mainRenderTargetView);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }

            init = true;
        }
        else
            return oPresent(pSwapChain, SyncInterval, Flags);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    __try
    {
        conglomerate.renderer.menu.render();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    __try
    {
    conglomerate.renderer.hud.render();
    conglomerate.renderer.spectatorList.render();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    // Always call esp() to allow individual components to be rendered
    __try
    {
        conglomerate.renderer.visuals.esp();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    ImGui::Render();
    pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return oPresent(pSwapChain, SyncInterval, Flags);
}

DWORD WINAPI MainThread(LPVOID lpReserved)
{
    bool init_hook = false;
    do
    {
        // hook hkPresent and init cheat
        if (!init_hook) {
            if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
            {
                // kiero::bind reports failure through its return value. The old
                // code set init_hook unconditionally, so a failed bind left the
                // cheat never initialising and never retrying.
                if (kiero::bind(8, (void**)&oPresent, hkPresent) == kiero::Status::Success)
                {
                    init_hook = true;
                }
                else
                {
                    kiero::shutdown();
                }
            }
        }

        // Otherwise this loop spins a core flat while it waits for F4.
        Sleep(100);
    } while (!GetAsyncKeyState(VK_F4));

    if (oWndProc != nullptr)
    {
        // restore wnd proc
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oWndProc));

        // invalidate old wnd proc
        oWndProc = nullptr;
    }

    kiero::shutdown();

    // destroy minhook
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    // close thread
    FreeLibraryAndExitThread(reinterpret_cast<HMODULE>(lpReserved), EXIT_SUCCESS);

    return TRUE;
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hMod);
        CreateThread(nullptr, 0, MainThread, hMod, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        kiero::shutdown();
        break;
    }
    return TRUE;
}
