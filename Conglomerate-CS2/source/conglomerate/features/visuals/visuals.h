#pragma once
#include <string>
#include "../../utils/math/viewmatrix/viewmatrix.h"
#include "..\..\..\cs2\entity\C_CSPlayerPawn\C_CSPlayerPawn.h"
#include "..\..\..\cs2\entity\C_BaseEntity\C_BaseEntity.h"

class LocalPlayerCached {
public:
	int handle;
	int health;
	int team;
	bool alive;
	Vector_t poisition;
	void reset() {
		poisition = Vector_t();
		team = 0;
		health = 0;
		handle = 0;
		alive = false;
	}
};

enum PlayerType_t : int
{
	none = -1,
	enemy = 0,
	team = 1,
};

struct PlayerCache
{
	PlayerCache(C_BaseEntity* a1, C_CSPlayerPawn* plyr, CBaseHandle hdl, PlayerType_t typ, int hp, const char* nm,
		const char* wep_name, Vector_t pos, Vector_t viewOff, int teamnm, bool isScoped, float flashTime, bool carriesC4) :
		entity(a1), player(plyr), handle(hdl), type(typ), health(hp), name(nm ? nm : "unknown"),
		weapon_name(wep_name ? wep_name : "unknown"), position(pos), viewOffset(viewOff), team_num(teamnm), scoped(isScoped), flashDuration(flashTime), carriesC4(carriesC4) { }

	C_BaseEntity* entity;
	C_CSPlayerPawn* player;
	CBaseHandle handle;
	PlayerType_t type;
	int health;
	// Owned copy: the game's player name pointer can be freed/recycled between
	// the frame we cache it and the frame we draw with it.
	std::string name;
	std::string weapon_name;
	Vector_t position;
	Vector_t viewOffset;
	int team_num;
	bool scoped;
	float flashDuration;
	bool carriesC4;
};

namespace Esp {
	class Visuals {
	public:
		void init();
		void esp();
	private:
		ViewMatrix viewMatrix;
	};
	void cache();
}

extern LocalPlayerCached cached_local;
extern std::vector<PlayerCache> cached_players;
