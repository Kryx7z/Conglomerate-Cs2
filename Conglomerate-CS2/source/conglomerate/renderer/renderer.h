#pragma once

#include "../menu/menu.h"
#include "../features/visuals/visuals.h"
#include "../menu/hud.h"
#include "../features/spectatorlist/spectatorlist.h"

class Renderer {
public:
	Menu menu;
	Esp::Visuals visuals;
	Hud hud;
	SpectatorList spectatorList;
};
