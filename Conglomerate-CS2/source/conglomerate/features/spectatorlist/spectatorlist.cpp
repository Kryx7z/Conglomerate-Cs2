#include "spectatorlist.h"

#include "../../../../external/imgui/imgui.h"

#include "../../config/config.h"
#include "../../interfaces/interfaces.h"
#include "../../interfaces/CGameEntitySystem/CGameEntitySystem.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"

void SpectatorList::render()
{
	if (!Config::spectatorList || !I::EngineClient || !I::EngineClient->valid() ||
		!I::GameEntity || !I::GameEntity->Instance)
		return;

	CCSPlayerController* localController = nullptr;
	const int highestIndex = I::GameEntity->Instance->GetHighestEntityIndex();

	for (int i = 1; i <= highestIndex; ++i)
	{
		C_BaseEntity* entity = I::GameEntity->Instance->Get(i);
		if (!entity || !entity->IsPlayerController())
			continue;

		auto controller = reinterpret_cast<CCSPlayerController*>(entity);
		if (controller->IsLocalPlayer())
		{
			localController = controller;
			break;
		}
	}

	if (!localController || !localController->m_hPawn().valid())
		return;

	const CBaseHandle localPawnHandle = localController->m_hPawn();
	auto localPawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(localPawnHandle);
	if (!localPawn || localPawn->m_iHealth() <= 0)
		return;

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

	ImGui::End();
	ImGui::PopStyleColor(2);
}
