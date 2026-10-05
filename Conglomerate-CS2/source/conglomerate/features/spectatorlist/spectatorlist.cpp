#include "spectatorlist.h"

#include "../../../../external/imgui/imgui.h"

#include "../../config/config.h"
#include "../../interfaces/interfaces.h"
#include "../../interfaces/CGameEntitySystem/CGameEntitySystem.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../utils/debug_console.h"

void SpectatorList::render()
{
	if (!Config::spectatorList)
		return;
	DebugConsole::once("spectators.enabled", "[runtime] spectator list enabled and render callback reached");
	if (!I::GameEntity || !I::GameEntity->Instance)
	{
		DebugConsole::rateLimited("spectators.interfaces", "[runtime] spectator list waiting: entity system unavailable");
		return;
	}

	CCSPlayerController* localController = nullptr;
	constexpr int highestIndex = 64;
	int controllersFound = 0;
	int localFlagsFound = 0;

	for (int i = 1; i <= highestIndex; ++i)
	{
		C_BaseEntity* entity = I::GameEntity->Instance->Get(i);
		if (!entity || !entity->IsPlayerController())
			continue;
		++controllersFound;

		auto controller = reinterpret_cast<CCSPlayerController*>(entity);
		if (controller->IsLocalPlayer())
		{
			++localFlagsFound;
			localController = controller;
			break;
		}
	}

	if (!localController)
	{
		DebugConsole::rateLimited("spectators.local_controller",
			"[runtime] spectator list found no local controller: controllers=%d local_flags=%d",
			controllersFound, localFlagsFound);
		return;
	}

	const CBaseHandle localPawnHandle = localController->m_hPlayerPawn();
	if (!localPawnHandle.valid())
	{
		DebugConsole::rateLimited("spectators.local_pawn_handle",
			"[runtime] spectator list local controller found but m_hPlayerPawn is invalid");
		return;
	}

	auto localPawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(localPawnHandle);
	if (!localPawn || localPawn->m_iHealth() <= 0)
	{
		DebugConsole::rateLimited("spectators.local_pawn", "[runtime] spectator list local pawn is missing or not alive");
		return;
	}

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.11f, 0.13f, 0.7f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.30f, 0.30f, 0.7f));
	ImGui::SetNextWindowSize(ImVec2(220.0f, 120.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 240.0f, 100.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Spectator List", nullptr,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

	ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "SPECTATORS:");
	int spectatorCount = 0;

	for (int i = 1; i <= highestIndex; ++i)
	{
		C_BaseEntity* entity = I::GameEntity->Instance->Get(i);
		if (!entity || !entity->IsPlayerController())
			continue;

		auto controller = reinterpret_cast<CCSPlayerController*>(entity);
		if (controller == localController || controller->m_bPawnIsAlive())
			continue;

		const CBaseHandle observerHandle = controller->m_hObserverPawn();
		if (!observerHandle.valid())
			continue;

		auto observerPawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(observerHandle);
		if (!observerPawn)
			continue;

		auto observerServices = observerPawn->m_pObserverServices();
		if (!observerServices || observerServices->m_hObserverTarget() != localPawnHandle)
			continue;

		const char* name = controller->m_sSanitizedPlayerName();
		ImGui::TextUnformatted(name && *name ? name : "unknown");
		++spectatorCount;
	}

	if (spectatorCount == 0)
		ImGui::TextDisabled("Nobody");
	DebugConsole::once("spectators.rendered", "[runtime] spectator list rendered; spectator count=%d", spectatorCount);

	ImGui::End();
	ImGui::PopStyleColor(2);
}
