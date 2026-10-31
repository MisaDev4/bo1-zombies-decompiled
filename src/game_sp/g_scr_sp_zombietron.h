#pragma once
// Lane Z: Dead Ops Arcade (map zombietron) builtins, src/game_sp/g_scr_sp_zombietron.cpp.

// zombies: clears the client's SP clientState score.scoreMultiplier (kept outside KB's clientState_s).
void G_SP_ClearZombietronClientState(unsigned int clientNum);

// zombies: zombietron's HUD (ownerdraw 354, cg_sp_hud.cpp) shows each player's boosters (SP score.downs), lives
// (score.revives), weapon-time meter (score.headshots) and score multiplier (score.scoreMultiplier). KB's
// clientState score has none of them, so on zombietron they travel in the per-client scoreboard columns
// (G_PopulateMatchState), headshots as in Five and the rest under column types zombies never shows. KB
// transport, not SP's wire format.
#define SB_ZT_BOOSTERS SB_TYPE_DEFENDS
#define SB_ZT_LIVES SB_TYPE_PLANTS
#define SB_ZT_MULTIPLIER SB_TYPE_DEFUSES
enum scoreboardColumnType_t : __int32;
struct gclient_s;
// Writes value into the client's scoreboard column of that type, when the match state has one.
void G_SP_SetScoreColumn(gclient_s *client, scoreboardColumnType_t columnType, int value);

// zombies: the objective's 3D marker text (Objective_Set3D) as a CS_LOCALIZED_STRINGS index, 0 for none.
struct objective_t;
void G_SP_SetObjective3DText(objective_t *obj, int localizedStringIndex);
int G_SP_GetObjective3DText(const objective_t *obj);
