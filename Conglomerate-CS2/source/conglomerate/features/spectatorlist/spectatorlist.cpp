#include "spectatorlist.h"

#include "../../../../external/imgui/imgui.h"

#include "../../config/config.h"
#include "../../interfaces/interfaces.h"
#include "../../interfaces/CGameEntitySystem/CGameEntitySystem.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../utils/debug_console.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
	const ImVec4 kSpectatorPurple(168.0f / 255.0f, 155.0f / 255.0f, 242.0f / 255.0f, 1.0f);
	const ImVec4 kSpectatorBlack(5.0f / 255.0f, 5.0f / 255.0f, 5.0f / 255.0f, 0.88f);

	void CenterText(const char* text)
	{
		const float textWidth = ImGui::CalcTextSize(text).x;
		ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(),
			(ImGui::GetWindowWidth() - textWidth) * 0.5f));
		ImGui::TextUnformatted(text);
	}
}

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

	std::vector<std::string> spectatorNames;
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
		spectatorNames.emplace_back(name && *name ? name : "unknown");
	}

	const float displayHeight = ImGui::GetIO().DisplaySize.y;
	const float desiredHeight = 76.0f + static_cast<float>((std::max<std::size_t>)(spectatorNames.size(), 1)) * 22.0f;
	const float windowHeight = (std::min)(desiredHeight, (std::max)(120.0f, displayHeight - 40.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, kSpectatorBlack);
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(168.0f / 255.0f, 155.0f / 255.0f, 242.0f / 255.0f, 0.62f));
	ImGui::PushStyleColor(ImGuiCol_Text, kSpectatorPurple);
	ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.30f, 0.27f, 0.45f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 9.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 5.0f));
	ImGui::SetNextWindowSize(ImVec2(235.0f, windowHeight), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 245.0f, 100.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Spectators", nullptr,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

	CenterText("Spectators");
	ImGui::Separator();
	for (const auto& name : spectatorNames)
		CenterText(name.c_str());
	if (spectatorNames.empty())
		CenterText("Nobody");
	DebugConsole::once("spectators.rendered", "[runtime] spectator list rendered; spectator count=%d",
		static_cast<int>(spectatorNames.size()));

	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(4);
}
