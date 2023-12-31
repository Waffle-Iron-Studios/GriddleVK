//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Randy Heit
// Copyright 2002-2016 Christoph Oelckers
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Internally used data structures for virtually everything,
//	 key definitions, lots of other stuff.
//
//-----------------------------------------------------------------------------

#ifndef __DOOMDEF_H__
#define __DOOMDEF_H__

#include <stdio.h>
#include <string.h>

//
// Global parameters/defines.
//

// Game mode handling - identify IWAD version
//	to handle IWAD dependend animations etc.
typedef enum
{
	shareware,		// DOOM 1 shareware, E1, M9
	registered,		// DOOM 1 registered, E3, M27
	commercial,		// DOOM 2 retail, E1 M34
	// DOOM 2 german edition not handled
	retail,			// DOOM 1 retail, E4, M36
	undetermined	// Well, no IWAD found.
  
} GameMode_t;


// If rangecheck is undefined, most parameter validation debugging code
// will not be compiled
#ifndef NORANGECHECKING
#ifndef RANGECHECK
#define RANGECHECK
#endif
#endif

// State updates, number of tics / second.
constexpr int TICRATE = 35;

// Global constants that were defines.
enum
{
	// The maximum number of players, multiplayer/networking.
	MAXPLAYERS = 8,

	// Amount of damage done by a telefrag.
	TELEFRAG_DAMAGE = 1000000
};

inline int Tics2Seconds(int tics)
{
	return tics / TICRATE;
}



typedef float skill_t;

/*
enum ESkillLevels
{
	sk_baby,
	sk_easy,
	sk_medium,
	sk_hard,
	sk_nightmare
};
*/



#define TELEFOGHEIGHT			(gameinfo.telefogheight)

//
// DOOM keyboard definition. Everything below 0x100 matches
// a mode 1 keyboard scan code.
//

#include "keydef.h"

// [RH] dmflags bits (based on Q2's)
enum : unsigned
{
	DF_NO_HEALTH			= 1 << 0,	// Do not spawn health items (DM)
	DF_NO_ITEMS				= 1 << 1,	// Do not spawn powerups (DM)
	DF_WEAPONS_STAY			= 1 << 2,	// Leave weapons around after pickup (DM)
	DF_FORCE_FALLINGZD		= 1 << 3,	// Falling too far hurts (old ZDoom style)
	DF_FORCE_FALLINGHX		= 2 << 3,	// Falling too far hurts (Hexen style)
	DF_FORCE_FALLINGST		= 3 << 3,	// Falling too far hurts (Strife style)
//							  1 << 5	-- this space left blank --
	DF_SAME_LEVEL			= 1 << 6,	// Stay on the same map when someone exits (DM)
	DF_SPAWN_FARTHEST		= 1 << 7,	// Spawn players as far as possible from other players (DM)
	DF_FORCE_RESPAWN		= 1 << 8,	// Automatically respawn dead players after respawn_time is up (DM)
	DF_NO_ARMOR				= 1 << 9,	// Do not spawn armor (DM)
	DF_NO_EXIT				= 1 << 10,	// Kill anyone who tries to exit the level (DM)
	DF_INFINITE_AMMO		= 1 << 11,	// Don't use up ammo when firing
	DF_NO_MONSTERS			= 1 << 12,	// Don't spawn monsters (replaces -nomonsters parm)
	DF_MONSTERS_RESPAWN		= 1 << 13,	// Monsters respawn sometime after their death (replaces -respawn parm)
	DF_ITEMS_RESPAWN		= 1 << 14,	// Items other than invuln. and invis. respawn
	DF_FAST_MONSTERS		= 1 << 15,	// Monsters are fast (replaces -fast parm)
	DF_NO_JUMP				= 1 << 16,	// Don't allow jumping
	DF_YES_JUMP				= 2 << 16,
	DF_NO_FREELOOK			= 1 << 18,	// Don't allow freelook
	DF_YES_FREELOOK			= 2 << 18,
	DF_NO_FOV				= 1 << 20,	// Only let the arbitrator set FOV (for all players)
	DF_NO_COOP_WEAPON_SPAWN	= 1 << 21,	// Don't spawn multiplayer weapons in coop games
	DF_NO_CROUCH			= 1 << 22,	// Don't allow crouching
	DF_YES_CROUCH			= 2 << 22,	//
	DF_COOP_LOSE_INVENTORY	= 1 << 24,	// Lose all your old inventory when respawning in coop
	DF_COOP_LOSE_KEYS		= 1 << 25,	// Lose keys when respawning in coop
	DF_COOP_LOSE_WEAPONS	= 1 << 26,	// Lose weapons when respawning in coop
	DF_COOP_LOSE_ARMOR		= 1 << 27,	// Lose armor when respawning in coop
	DF_COOP_LOSE_POWERUPS	= 1 << 28,	// Lose powerups when respawning in coop
	DF_COOP_LOSE_AMMO		= 1 << 29,	// Lose ammo when respawning in coop
	DF_COOP_HALVE_AMMO		= 1 << 30,	// Lose half your ammo when respawning in coop (but not less than the normal starting amount)
	DF_INSTANT_REACTION		= 1u << 31,	// Monsters react instantly
};

// [BC] More dmflags. w00p!
enum : unsigned
{
//	DF2_YES_IMPALING		= 1 << 0,	// Player gets impaled on MF2_IMPALE items
	DF2_YES_WEAPONDROP		= 1 << 1,	// Drop current weapon upon death
//	DF2_NO_RUNES			= 1 << 2,	// Don't spawn runes
//	DF2_INSTANT_RETURN		= 1 << 3,	// Instantly return flags and skulls when player carrying it dies (ST/CTF)
	DF2_NO_TEAM_SWITCH		= 1 << 4,	// Do not allow players to switch teams in teamgames
//	DF2_NO_TEAM_SELECT		= 1 << 5,	// Player is automatically placed on a team.
	DF2_YES_DOUBLEAMMO		= 1 << 6,	// Double amount of ammo that items give you like skill 1 and 5 do
	DF2_YES_DEGENERATION	= 1 << 7,	// Player slowly loses health when over 100% (Quake-style)
	DF2_NO_FREEAIMBFG		= 1 << 8,	// Disallow BFG freeaiming. Prevents cheap BFG frags by aiming at floor or ceiling
	DF2_BARRELS_RESPAWN		= 1 << 9,	// Barrels respawn (duh)
	DF2_YES_RESPAWN_INVUL	= 1 << 10,	// Player is temporarily invulnerable when respawned
//	DF2_COOP_SHOTGUNSTART	= 1 << 11,	// All playres start with a shotgun when they respawn
	DF2_SAME_SPAWN_SPOT		= 1 << 12,	// Players respawn in the same place they died (co-op)
	DF2_YES_KEEPFRAGS		= 1 << 13,	// Don't clear frags after each level
	DF2_NO_RESPAWN			= 1 << 14,	// Player cannot respawn
	DF2_YES_LOSEFRAG		= 1 << 15,	// Lose a frag when killed. More incentive to try to not get yerself killed
	DF2_INFINITE_INVENTORY	= 1 << 16,	// Infinite inventory.
	DF2_KILL_MONSTERS		= 1 << 17,	// All monsters must be killed before the level exits.
	DF2_NO_AUTOMAP			= 1 << 18,	// Players are allowed to see the automap.
	DF2_NO_AUTOMAP_ALLIES	= 1 << 19,	// Allies can been seen on the automap.
	DF2_DISALLOW_SPYING		= 1 << 20,	// You can spy on your allies.
	DF2_CHASECAM			= 1 << 21,	// Players can use the chasecam cheat.
	DF2_NOSUICIDE			= 1 << 22,	// Players are not allowed to suicide.
	DF2_NOAUTOAIM			= 1 << 23,	// Players cannot use autoaim.
	DF2_DONTCHECKAMMO		= 1 << 24,	// Don't Check ammo when switching weapons.
	DF2_KILLBOSSMONST		= 1 << 25,	// Kills all monsters spawned by a boss cube when the boss dies
	DF2_NOCOUNTENDMONST		= 1 << 26,	// Do not count monsters in 'end level when dying' sectors towards kill count
	DF2_RESPAWN_SUPER		= 1 << 27,	// Respawn invulnerability and invisibility
	DF2_NO_COOP_THING_SPAWN	= 1 << 28,	// Don't spawn multiplayer things in coop games
	DF2_ALWAYS_SPAWN_MULTI	= 1 << 29,	// Always spawn multiplayer items
	DF2_NOVERTSPREAD		= 1 << 30,	// Don't allow vertical spread for hitscan weapons (excluding ssg)
	DF2_NO_EXTRA_AMMO		= 1u << 31,	// Don't add extra ammo when picking up weapons (like in original Doom)
};

// [Nash] dmflags3 in 2023 let's gooooo
enum : unsigned
{
	DF3_NO_PLAYER_CLIP		= 1 << 0,	// Players can walk through and shoot through each other
};

// phares 3/20/98:
//
// Player friction is variable, based on controlling
// linedefs. More friction can create mud, sludge,
// magnetized floors, etc. Less friction can create ice.

#define MORE_FRICTION_VELOCITY	(15000/65536.)	// mud factor based on velocity
#define ORIG_FRICTION			(0xE800/65536.)	// original value
#define ORIG_FRICTION_FACTOR	(2048/65536.)	// original value
#define FRICTION_LOW			(0xf900/65536.)
#define FRICTION_FLY			(0xeb00/65536.)


#define BLINKTHRESHOLD (4*32)

#endif	// __DOOMDEF_H__
