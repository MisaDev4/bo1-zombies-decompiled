#include <qcommon/cm_mapkit.h>
#include <universal/profile.h>
#include <algorithm>
#include <game_sp/g_scr_sp_zombietron.h>
#include <windows.h>
#include <dbghelp.h>
#include <clientscript/cscr_compiler.h>
#include <clientscript/cscr_parser.h>
#include <clientscript/cscr_vm.h>
#include <game_sp/g_sp_headless_throw.h>
#include <qcommon/actor_model_state.h>
#include <sound/snd_bo1_trace.h>
#include <game_sp/g_anim_commands_sp.h>
#include <game_sp/g_sp_ext.h>
#include <game_sp/g_sp_measure.h>
#include <game_sp/g_sp_headless_move.h>
#include <game_sp/g_sp_client.h>
#include "g_main_mp.h"
#include <game_sp/g_sp_savegame.h>
#include <game_sp/g_sp_loadgame.h>
#include <game_sp/g_sp_levelstart.h>
#include <cgame/cg_event.h>
#include <game_sp/g_scr_sp_entity.h>
#include <game_sp/g_sp_level_exit.h>
#include <game_sp/g_scr_sp_ai.h>
#include <game_sp/g_sp_player_state.h>
#include <universal/dvar.h>
#include <gfx_d3d/r_reflection_probe.h>
#include <universal/assertive.h>
#include <game/teams.h>
#include <demo/demo_common.h>
#include <game/pathnode.h>
#include <universal/com_memory.h>
#include <clientscript/cscr_stringlist.h>
#include <bgame/bg_misc.h>
#include <bgame/bg_weapons.h>
#include <clientscript/scr_const.h>
#include <qcommon/common.h>
#include <qcommon/threads.h>
#include <qcommon/cm_load.h>
#include <game/enthandle.h>
#include <game/g_helicopter1.h>
#include <game/turret.h>
#include <game/g_missile.h>
#include <turret/turret_placement.h>
#include <DynEntity/DynEntity_server.h>
#include <qcommon/dobj_management.h>
#include "g_utils_mp.h"
#include <game/g_targets.h>
#include "actor_mp.h"
#include <gfx_d3d/r_dvars.h>
#include <bgame/bg_dog_animations_mp.h>
#include "g_active_mp.h"
#include <glass/glass_server.h>
#include <universal/com_files.h>
#include <game/g_hudelem.h>
#include <clientscript/cscr_vm.h>
#include <client_mp/sv_client_mp.h>
#include <server/sv_game.h>
#include <universal/com_shared.h>
#include <server_mp/sv_main_mp.h>
#include <server_mp/sv_init_mp.h>
#include <game/actor_script_cmd.h>
#include <client_mp/cl_cgame_mp.h>
#include "g_misc_mp.h"
#include "g_combat_mp.h"
#include <bgame/bg_fire.h>
#include <game/g_debug.h>
#include <ik/ik.h>
#include "g_spawn_mp.h"
#include "g_team_mp.h"
#include "g_spawnsystem_mp.h"
#include <game/actor_corpse.h>
#include <live/live_steam_server.h>
#include <bgame/bg_pmove.h>
#include <server/sv_world.h>
#include <flame/flame_damage.h>
#include <game/g_mover.h>
#include <game/g_player_corpse.h>
#include <game/g_weapon.h>
#include <client/cl_debugdata.h>
#include <game/g_svcmds.h>
#include <ui_mp/ui_gametype_custom_mp.h>
#include <qcommon/cm_world.h>
#include <bgame/bg_vehicle_anim.h>
#include <sound/snd_bank.h>
#include "g_cmds_mp.h"
#include <game/actor_spawner.h>
#include "pregame.h"
#include <game_sp/g_sp_playtrace.h>

const char *g_entcountNames[8] =
{
  "off",
  "basic entity statistics",
  "entities by type",
  "entities in server snapshot (NO_CLIENT unset)",
  "entities considered for delta (NO_CLIENT and CLIENT_ONCE unset)",
  "delta compared script_model breakdown",
  "entities in server snapshot by eType",
  NULL
};

static const char *g_entinfoTypeNames[4] =
{ "all", "AI only", "vehicle only", NULL };

const char *g_entinfoAITextNames[6] =
{ "all", "brief", "combat", "movement", "state", NULL };

const char *g_entinfoNames[9] =
{
  "off",
  "all ents / draw lines / draw info",
  "selected ent / draw lines / draw info",
  "selected ent / draw info",
  "all ents / draw goal lines and radii",
  "selected ent / draw goal lines and radii",
  "script models considered for network",
  "selected ent / animation tree",
  NULL
};

const char *moveOrientModeStrings[8] =
{
  "invalid",
  "dont_change",
  "motion",
  "enemy",
  "enemy_or_motion",
  "enemy_or_motion_sidestep",
  "goal",
  NULL
};

const dvar_t *g_connectpaths;
const dvar_t *g_loadScripts;
const dvar_t *g_cheats;
const dvar_t *g_erroronpathsnotconnected;
const dvar_t *sv_mapname;
const dvar_t *g_gametype;
const dvar_t *g_synchronousClients;
const dvar_t *g_log;
const dvar_t *g_logTimeStampInSeconds;
const dvar_t *g_logSync;
const dvar_t *g_password;
const dvar_t *g_banIPs;
const dvar_t *g_speed;
const dvar_t *g_knockback;
const dvar_t *g_maxDroppedWeapons;
const dvar_t *g_inactivity;
const dvar_t *g_debugDamage;
const dvar_t *g_debugBullets;
const dvar_t *g_vehicleDrawPath;
const dvar_t *ai_enableBadPlaces;
const dvar_t *g_ai;
const dvar_t *g_spawnai;
const dvar_t *g_dumpAIEvents;
const dvar_t *ai_turnRate;
const dvar_t *ai_useFacingTranslation;
const dvar_t *ai_useLeanRunAnimations;
const dvar_t *ai_useBetterLookahead;
const dvar_t *ai_slowdownMinYawDiff;
const dvar_t *ai_slowdownMaxYawDiff;
const dvar_t *ai_slowdownMinRate;
const dvar_t *ai_slowdownRateBlendFactor;
const dvar_t *ai_angularYawEnabled;
const dvar_t *ai_angularYawAccelRate;
const dvar_t *ai_angularYawDecelFactor;
const dvar_t *ai_corpseCount;
const dvar_t *ai_showNodes;
const dvar_t *ai_showNodesDist;
const dvar_t *ai_showNearestNode;
const dvar_t *ai_showVisData;
const dvar_t *ai_showVisDataDist;
const dvar_t *ai_showPaths;
const dvar_t *ai_debugFindPath;
const dvar_t *ai_debugFindPathDirect;
const dvar_t *ai_debugFindPathWidth;
const dvar_t *ai_debugFindPathLock;
const dvar_t *ai_debugClaimedNodes;
const dvar_t *ai_disableSpawn;
const dvar_t *ai_moveOrientMode;
const dvar_t *ai_pathNegotiationOverlapCost;
const dvar_t *ai_showPotentialThreatDir;
const dvar_t *ai_debugCoverEntityNum;
const dvar_t *ai_showBadPlaces;
const dvar_t *ai_showDodge;
const dvar_t *ai_noDodge;
const dvar_t *ai_pathMomentum;
const dvar_t *ai_debugMayMove;
const dvar_t *ai_showVolume;
const dvar_t *ai_debugAnimDeltas;
const dvar_t *ai_debugThreatSelection;
const dvar_t *ai_debugMeleeAttackSpots;
const dvar_t *ai_debugEntIndex;
const dvar_t *ai_eventDistFootstep;
const dvar_t *ai_eventDistFootstepLite;
const dvar_t *ai_eventDistNewEnemy;
const dvar_t *ai_eventDistReact;
const dvar_t *ai_eventDistPain;
const dvar_t *ai_eventDistDeath;
const dvar_t *ai_eventDistExplosion;
const dvar_t *ai_eventDistGrenadePing;
const dvar_t *ai_eventDistProjPing;
const dvar_t *ai_eventDistGunShot;
const dvar_t *ai_eventDistSilencedShot;
const dvar_t *ai_eventDistBullet;
const dvar_t *ai_eventDistBulletRunning;
const dvar_t *ai_eventDistProjImpact;
const dvar_t *ai_eventDistBadPlace;
const dvar_t *ai_playerNearAccuracy;
const dvar_t *ai_playerNearRange;
const dvar_t *ai_playerFarAccuracy;
const dvar_t *ai_playerFarRange;
const dvar_t *ai_threatUpdateInterval;
const dvar_t *ai_foliageIngoreDist;
const dvar_t *ai_friendlySuppression;
const dvar_t *ai_friendlySuppressionDist;
const dvar_t *ai_meleeRange;
const dvar_t *ai_meleeWidth;
const dvar_t *ai_meleeHeight;
const dvar_t *ai_meleeDamage;
const dvar_t *ai_maxAttackerCount;
const dvar_t *ai_noPathToEnemyGiveupTime;
const dvar_t *bullet_penetrationEnabled;
const dvar_t *g_entinfo;
const dvar_t *g_entinfo_type;
const dvar_t *g_entinfo_AItext;
const dvar_t *g_entinfo_maxdist;
const dvar_t *g_entinfo_scale;
const dvar_t *g_debugPlayerAnimScript;
const dvar_t *g_motd;
const dvar_t *g_playerCollisionEjectSpeed;
const dvar_t *g_dropForwardSpeed;
const dvar_t *g_dropUpSpeedBase;
const dvar_t *g_dropUpSpeedRand;
const dvar_t *g_dropHorzSpeedRand;
const dvar_t *g_clonePlayerMaxVelocity;
const dvar_t *voice_global;
const dvar_t *voice_localEcho;
const dvar_t *voice_deadChat;
const dvar_t *g_allowVote;
const dvar_t *g_allow_teamchange;
const dvar_t *g_listEntity;
const dvar_t *g_listEntityCounts;
const dvar_t *g_entsInSnapshot;
const dvar_t *g_maxEntsInSnapshot;
const dvar_t *g_deadChat;
const dvar_t *g_voiceChatTalkingDuration;
const dvar_t *g_TeamIcon_Axis;
const dvar_t *g_TeamIcon_Allies;
const dvar_t *g_TeamIcon_Free;
const dvar_t *g_TeamIcon_Spectator;
const dvar_t *g_ScoresColor_MyTeam;
const dvar_t *g_ScoresColor_EnemyTeam;
const dvar_t *g_ScoresColor_Spectator;
const dvar_t *g_ScoresColor_Free;
const dvar_t *g_ScoresColor_Allies;
const dvar_t *g_ScoresColor_Axis;
const dvar_t *g_ScoresPing_Interval;
const dvar_t *g_TeamName_Allies;
const dvar_t *g_TeamName_Axis;
const dvar_t *g_TeamColor_Allies;
const dvar_t *g_TeamColor_Axis;
const dvar_t *g_TeamColor_MyTeam;
const dvar_t *g_TeamColor_EnemyTeam;
const dvar_t *g_TeamColor_MyTeamAlt;
const dvar_t *g_TeamColor_EnemyTeamAlt;
const dvar_t *g_TeamColor_Squad;
const dvar_t *g_TeamColor_Spectator;
const dvar_t *g_TeamColor_Free;
const dvar_t *g_debugLocDamage;
const dvar_t *g_debugLocHit;
const dvar_t *g_debugLocHitTime;
const dvar_t *g_smoothClients;
const dvar_t *g_antilag;
const dvar_t *g_oldVoting;
const dvar_t *g_voteAbstainWeight;
const dvar_t *g_NoScriptSpam;
const dvar_t *g_friendlyfireDist;
const dvar_t *g_friendlyNameDist;
const dvar_t *melee_debug;
const dvar_t *radius_damage_debug;
const dvar_t *player_throwbackInnerRadius;
const dvar_t *player_throwbackOuterRadius;
const dvar_t *player_useRadius;
const dvar_t *player_MGUseRadius;
const dvar_t *vehicle_useRadius;
const dvar_t *g_minGrenadeDamageSpeed;
//const dvar_t *g_compassShowEnemies;
const dvar_t *pickupPrints;
const dvar_t *g_revive;
const dvar_t *g_dumpAnims;
const dvar_t *g_useholdtime;
const dvar_t *g_useholdspawndelay;
const dvar_t *g_redCrosshairs;
const dvar_t *g_mantleBlockTimeBuffer;
const dvar_t *g_vehicleDebug;
const dvar_t *vehGunnerSplashDamage;
const dvar_t *turretPlayerAvoidScale;
const dvar_t *g_enableAttachWeaponFix;
const dvar_t *anim_deltas_debug;
const dvar_t *g_destructibleDraw;
const dvar_t *g_debugServerAiming;
const dvar_t *g_fogColorReadOnly;
const dvar_t *g_fogStartDistReadOnly;
const dvar_t *g_fogHalfDistReadOnly;
const dvar_t *vehPlaneRollDeadZone;
const dvar_t *vehPlaneRollAccel;
const dvar_t *vehPlanePitchAccel;
const dvar_t *vehPlaneYawSpeed;
const dvar_t *vehPlaneYawFromRollScale;
const dvar_t *vehPlaneLiftForce;
const dvar_t *vehPlaneFakeLiftForce;
const dvar_t *vehPlaneLowSpeed;
const dvar_t *vehPlaneGravityForce;
const dvar_t *vehicle_switch_seat_delay;
const dvar_t *vehicle_damage_max_shielding;
const dvar_t *vehicle_damage_zone_front;
const dvar_t *vehicle_damage_zone_side;
const dvar_t *vehicle_damage_zone_rear;
const dvar_t *vehicle_damage_zone_under;
const dvar_t *vehicle_damage_bullet;
const dvar_t *vehicle_damage_grenade;
const dvar_t *vehicle_damage_projectile;
const dvar_t *vehicle_damage_bouncing_betty;
const dvar_t *vehicle_damage_satchel_charge;
const dvar_t *vehicle_damage_sticky_grenade;
const dvar_t *vehicle_piece_damagesfx_threshold;
const dvar_t *vehicle_destructible_damage_grenade;
const dvar_t *vehicle_destructible_damage_bouncing_betty;
const dvar_t *vehicle_destructible_damage_satchel_charge;
const dvar_t *vehicle_destructible_damage_sticky_grenade;
const dvar_t *vehicle_destructible_damage_grenade_radius;
const dvar_t *vehicle_destructible_damage_bouncing_betty_radius;
const dvar_t *vehicle_destructible_damage_satchel_charge_radius;
const dvar_t *vehicle_destructible_damage_sticky_grenade_radius;
const dvar_t *vehicle_destructible_damage_projectile_radius;
const dvar_t *vehicle_perk_leadfoot_speed_increase;
const dvar_t *g_turretServerPitchMin;
const dvar_t *g_turretServerPitchMax;
const dvar_t *g_turretBipodOffset;

bgsAnim_s level_bgsAnim;
level_locals_t level;

int g_timed_radius_damage_count;
TIMED_RADIUS_DAMAGE g_timed_radius_damage[512];

gentity_s g_entities[MAX_GENTITIES_SV];
sentient_s g_sentients[MAX_SENTIENTS_CAP]; // mod: retail 48; only the first g_maxSentients are used
actor_s g_actors[MAX_ACTORS_CAP]; // mod: retail 32; only the first g_maxActors are used
int g_entNumBits = 10; // mod (L25): networked entity number bits (q_shared.h)
int g_entNumCscEnd = ENTNUM_CSC_END; // mod (L55): q_shared.h; 1280 with bo1_mod_netents 2
int g_scrEntNumLimit = MAX_GENTITIES; // mod (L25): q_shared.h
int g_maxActors = MAX_ACTORS_RETAIL; // mod: bo1_mod_maxactors, set once per map in G_InitGame
int g_maxSentients = MAX_SENTIENTS_RETAIL;
static const dvar_t *g_modMaxActors;
gclient_s g_clients[32];

playerState_s g_defaultPlayerState;

int __cdecl G_GetTime()
{
    return level.time;
}

void __cdecl G_RegisterConnectPaths()
{
    g_connectpaths = _Dvar_RegisterInt("g_connectpaths", 0, 0, 3, 0, "Connect paths");
}

bool __cdecl G_OnlyConnectingPaths()
{
    return g_connectpaths->current.integer >= 2;
}

void __cdecl G_RegisterRegisterToolDvars()
{
    bool v0; // al

    G_RegisterConnectPaths();
    R_ReflectionProbeRegisterDvars();
    v0 = G_ExitAfterToolComplete();
    g_loadScripts = _Dvar_RegisterBool("g_loadScripts", !v0, 0, "Disable scripts from loading");
}

int __cdecl G_GetSavePersist()
{
    return level.savepersist;
}

void __cdecl G_SetSavePersist(int savepersist)
{
    level.savepersist = savepersist;
}

double __cdecl G_GetFogOpaqueDistSqrd()
{
    return level.fFogOpaqueDistSqrd;
}

int __cdecl G_GetClientScore(unsigned int clientNum)
{
    if ( clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1376,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return level.clients[clientNum].sess.cs.score.score;
}

int __cdecl G_GetClientKills(unsigned int clientNum)
{
    if ( clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1388,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return level.clients[clientNum].sess.cs.score.kills;
}

int __cdecl G_GetClientAssists(unsigned int clientNum)
{
    if ( clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1400,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return level.clients[clientNum].sess.cs.score.assists;
}

int __cdecl G_GetClientRank(unsigned int clientNum)
{
    if ( clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1412,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return level.clients[clientNum].sess.cs.rank;
}

int __cdecl G_GetClientPrestige(unsigned int clientNum)
{
    if ( clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1424,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return level.clients[clientNum].sess.cs.prestige;
}

team_t __cdecl G_GetClientTeam(unsigned int clientNum)
{
    if ( clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1436,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return level.clients[clientNum].sess.cs.team;
}

int __cdecl G_GetClientDeaths(unsigned int clientNum)
{
    if ( clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1449,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return level.clients[clientNum].sess.cs.score.deaths;
}

int __cdecl G_GetClientArchiveTime(unsigned int clientNum)
{
    if ( clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1462,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return level.clients[clientNum].sess.archiveTime;
}

void __cdecl G_SetClientArchiveTime(unsigned int clientNum, int time)
{
    if ( clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1475,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    level.clients[clientNum].sess.archiveTime = time;
}

MatchState *__cdecl G_GetMatchState()
{
    return &level.matchState;
}

clientState_s *__cdecl G_GetClientState(unsigned int clientNum)
{
    if ( !Demo_IsPlaying()
        && clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1501,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return &level.clients[clientNum].sess.cs;
}

gclient_s *__cdecl G_GetPlayerState(unsigned int clientNum)
{
    if ( !Demo_IsPlaying()
        && clientNum >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1518,
                    0,
                    "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                    clientNum,
                    level.maxclients) )
    {
        __debugbreak();
    }
    return &level.clients[clientNum];
}

int __cdecl G_GetClientSize()
{
    return 10720;
}

// zombies: false from G_InitGame's memset(&level) until it sets level.maxclients. A script compile error drops
// the server inside that window (GScr_LoadScripts runs first), and SV_FinalMessage then builds a snapshot
// header for the clients still connected from the previous level (the front end's local client).
bool __cdecl G_HasClientStates()
{
    return level.maxclients > 0;
}

void __cdecl G_FreeEntities(bool clearTargets)
{
    gentity_s *e; // [esp+0h] [ebp-Ch]
    int i; // [esp+8h] [ebp-4h]
    int ia; // [esp+8h] [ebp-4h]
    int ib; // [esp+8h] [ebp-4h]

    for ( i = 0; i < G_EntEnd(); i = G_EntNext(i) ) // mod (L25): server-only entity numbers 1024+
    {
        e = &g_entities[i];
        if ( e->r.inuse )
            G_FreeEntity(e);
    }
    if ( g_entities[ENTITYNUM_WORLD].r.inuse )
        G_FreeEntity(&g_entities[ENTITYNUM_WORLD]);
    if ( clearTargets )
        Targ_RemoveAll();
    if ( level.actors )
    {
        for ( ia = 0; ia < MAX_ACTORS; ++ia )
        {
            if ( level.actors[ia].inuse )
                Actor_ClearPileUp(&level.actors[ia]);
        }
    }

    for ( ib = 0; ib < 32; ++ib )
        level.droppedWeaponCue[ib].setEnt(0);

    if ( g_entities[1023].r.inuse
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1582,
                    0,
                    "%s",
                    "!g_entities[ENTITYNUM_NONE].r.inuse") )
    {
        __debugbreak();
    }
    level.num_entities = 0;
    level.firstFreeEnt = 0;
    level.lastFreeEnt = 0;
    Path_CheckLinkLeaks();
    Path_CheckUserCountLeaks();
}

void *__cdecl Hunk_AllocXAnimServer(unsigned int size)
{
    return Hunk_AllocLow(size, "Hunk_AllocXAnimServer", 13);
}

bool __cdecl G_ExitAfterConnectPaths()
{
    return g_connectpaths && g_connectpaths->current.integer >= 2;
}

bool __cdecl G_ExitAfterToolComplete()
{
    return G_ExitAfterConnectPaths() || R_ReflectionProbeGenerateExitWhenDone();
}

bool __cdecl G_ExitOnComError(int code)
{
    if ( g_connectpaths && g_connectpaths->current.integer == 3 && !code )
        return 1;
    if ( g_connectpaths->current.integer >= 2 && code == 1 )
        return 1;
    return r_reflectionProbeGenerate && r_reflectionProbeGenerate->current.enabled;
}

int __cdecl G_IsServerGameSystem(int clientNum)
{
    if ( !g_debugPlayerAnimScript )
        return 0;
    if ( clientNum != g_debugPlayerAnimScript->current.integer )
        return 0;
    if (bgs != &level_bgs)
        return 0;
    Com_Printf(19, "(%i) ", level.time);
    return 1;
}

unsigned __int16 __cdecl G_GetWeaponAttachBone(clientInfo_t *ci, weapType_t weapType, weapInventoryType_t invType)
{
    if ( weapType == WEAPTYPE_GRENADE || weapType == WEAPTYPE_MINE )
    {
        if ( invType != WEAPINVENTORY_ITEM )
            return scr_const.tag_inhand;
    }
    else if ( ci->leftHandGun )
    {
        return SL_FindString(bg_weaponleftbone->current.string, SCRIPTINSTANCE_SERVER);
    }
    return SL_FindString(bg_weaponrightbone->current.string, SCRIPTINSTANCE_SERVER);
}

void G_FreeAnimTreeInstances()
{
    int i; // [esp+0h] [ebp-4h]

    for ( i = 0; i < com_maxclients->current.integer; ++i )
    {
        if ( level_bgs.clientinfo[i].pXAnimTree )
        {
            XAnimFreeTree(level_bgs.clientinfo[i].pXAnimTree, 0, SCRIPTINSTANCE_SERVER);
            level_bgs.clientinfo[i].pXAnimTree = 0;
        }
    }
    for ( i = 0; i < 4; ++i )
    {
        if (g_scr_data.playerCorpseInfo[i].tree)
        {
            XAnimFreeTree(g_scr_data.playerCorpseInfo[i].tree, 0, SCRIPTINSTANCE_SERVER);
            g_scr_data.playerCorpseInfo[i].tree = NULL;
        }
    }
    for ( i = 0; i < MAX_ACTORS_CAP; ++i ) // zombies: SP actor tree bank (SP levels only); mod: + slots 32+
    {
        if (G_ActorXAnimTreeSlot(i))
        {
            XAnimFreeTree(G_ActorXAnimTreeSlot(i), 0, SCRIPTINSTANCE_SERVER);
            G_ActorXAnimTreeSlot(i) = NULL;
        }
    }
    for ( i = 0; i < MAX_ACTOR_CORPSES_SP; ++i )
    {
        if (g_scr_data.actorCorpseInfo[i].tree)
        {
            XAnimFreeTree(g_scr_data.actorCorpseInfo[i].tree, 0, SCRIPTINSTANCE_SERVER);
            g_scr_data.actorCorpseInfo[i].tree = NULL;
        }
    }
}

void __cdecl    G_InitGame(int levelTime, int randomSeed, int restart, int registerDvars,
    bool loadGame, SPSaveGame **save)
{
    if ( !Sys_IsMainThread()
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    2026,
                    0,
                    "%s",
                    "Sys_IsMainThread()") )
    {
        __debugbreak();
    }
    
    PROF_SCOPED("G_InitGame");

    Com_Printf(15, "------- Game Initialization -------\n");
    Com_Printf(15, "gamename: %s\n", "Call of Duty�");
    Com_Printf(15, "gamedate: %s\n", "Nov    5 2010");
    Rope_InitRopes();
    Swap_Init();
    // mod: bo1_mod_maxactors (default 32 = retail) raises the actor and sentient counts for the whole map (read before
    // EntHandle::Init: the handle pool grows with it, L25)
    if (!g_modMaxActors)
        g_modMaxActors = _Dvar_RegisterInt("bo1_mod_maxactors", MAX_ACTORS_RETAIL, MAX_ACTORS_RETAIL, MAX_ACTORS_CAP, 0,
            "mod: AI actor slots (retail 32); read when a map starts");
    g_maxActors = g_modMaxActors->current.integer;
    g_maxSentients = MAX_SENTIENTS_RETAIL + g_maxActors - MAX_ACTORS_RETAIL;
    // mod (L25): bo1_mod_netents 1 (with bo1_mod_maxactors > 32) networks game entities 1537..2046 in 11-bit
    // entity numbers. Server side only so far: the client side is not ported (notes/L25-handoff.md), so -Client runs crash.
    static const dvar_t *g_modNetEnts;
    if (!g_modNetEnts)
        g_modNetEnts = _Dvar_RegisterInt("bo1_mod_netents", 0, 0, 2, 0, "mod: 11-bit networked entity numbers (1537..2046; 2 also 1280..1535, client-script / server-only entities then 1024..1279); read when a map starts");
    // mod (L43): bo1_mod_svprof 1 prints BO1_SVPROF (server frame by subsystem) every 5 s: G_ModSvProfEnd
    static const dvar_t *g_modSvProf;
    if (!g_modSvProf)
        g_modSvProf = _Dvar_RegisterInt("bo1_mod_svprof", 0, 0, 2, 0, "mod: server frame breakdown line every 5 s (capacity benchmark); 2 also samples native code");
    g_svProfOn = g_modSvProf->current.integer;
    // mod (L55): bo1_mod_snapprio 1 (default): past the 1024 snapshot cap keep non-zombies first, then zombies nearest
    // first (sv_snapshot_mp.cpp SV_BO1PrioritizeSnapshot); 0 = the L51 cut by entity number
    static const dvar_t *g_modSnapPrio;
    if (!g_modSnapPrio)
        g_modSnapPrio = _Dvar_RegisterInt("bo1_mod_snapprio", 1, 0, 1, 0, "mod: snapshot cap keeps non-zombies, then the nearest zombies; read when a map starts");
    extern int g_bo1SnapPrio;
    g_bo1SnapPrio = g_modSnapPrio->current.integer;
    // mod (L43 pass 6): bo1_mod_entboxcache 1: CM_AreaEntities_r rejects on a compact copy of each entity's box before
    // reading its gentity (cm_world.cpp); 2: checks the copy against the gentity instead (BO1_ENTBOX lines)
    static const dvar_t *g_modEntBoxCacheDvar;
    if (!g_modEntBoxCacheDvar)
        g_modEntBoxCacheDvar = _Dvar_RegisterInt("bo1_mod_entboxcache", 0, 0, 2, 0, "mod: entity box copy for the area entity query (1 use, 2 verify); read when a map starts");
    { extern int g_modEntBoxCache; g_modEntBoxCache = g_modEntBoxCacheDvar->current.integer; }
    // mod (L43 pass 8): bo1_mod_moveaway (actor_mp.cpp Actor_MoveAwayNoWorse)
    static const dvar_t *g_modMoveAwayDvar;
    if (!g_modMoveAwayDvar)
        g_modMoveAwayDvar = _Dvar_RegisterInt("bo1_mod_moveaway", 0, 0, 2, 0, "mod: Actor_MoveAwayNoWorse visits the actors in a box query instead of every actor slot (1 use, 2 verify); read when a map starts");
    // mod (L55): bo1_mod_svfast 1 = bo1_mod_sectorlist (cm_world.cpp), moveaway, teammove and sentientcopy 1 where 0
    static const dvar_t *g_modSvFastDvar;
    if (!g_modSvFastDvar)
        g_modSvFastDvar = _Dvar_RegisterInt("bo1_mod_svfast", 0, 0, 1, 0, "mod: 1 turns on the exact server options bo1_mod_sectorlist / moveaway / teammove / sentientcopy / entcontents / sentientscan (each 1) where their own dvar is 0; read when a map starts");
    const int svFast = g_modSvFastDvar->current.integer;
    // mod (L55): bo1_mod_entcontents - r.contents copy for the area entity query (cm_world.cpp); part of svfast
    static const dvar_t *g_modEntContentsDvar;
    if (!g_modEntContentsDvar)
        g_modEntContentsDvar = _Dvar_RegisterInt("bo1_mod_entcontents", 0, 0, 2, 0, "mod: entity contents copy for the area entity query (1 use, 2 verify); read when a map starts");
    static const dvar_t *g_modSentientScanDvar; // mod (L55): sentient.cpp; part of svfast
    if (!g_modSentientScanDvar)
        g_modSentientScanDvar = _Dvar_RegisterInt("bo1_mod_sentientscan", 0, 0, 2, 0, "mod: sentient team scans read a compact team copy (1 use, 2 verify); read when a map starts");
    { extern int g_modSentientScan; g_modSentientScan = g_modSentientScanDvar->current.integer ? g_modSentientScanDvar->current.integer : svFast; }
    { extern int g_modEntContentsCache; g_modEntContentsCache = g_modEntContentsDvar->current.integer ? g_modEntContentsDvar->current.integer : svFast; }
    { extern int g_modMoveAway; g_modMoveAway = g_modMoveAwayDvar->current.integer ? g_modMoveAwayDvar->current.integer : svFast; }
    // mod (L43q): exact bulk knowledge copy for large SP hordes; 2 runs and compares the original loop as well.
    static const dvar_t *g_modSentientCopyDvar;
    if (!g_modSentientCopyDvar)
        g_modSentientCopyDvar = _Dvar_RegisterInt("bo1_mod_sentientcopy", 0, 0, 2, 0, "mod: bulk spawn knowledge copy (1 use, 2 verify); read when a map starts");
    { extern int g_modSentientKnownMax[]; memset(g_modSentientKnownMax, 0, sizeof(int) * MAX_SENTIENTS_CAP); } // mod (L55): actor_mp.cpp
    { extern int g_modSentientCopy; g_modSentientCopy = zombiemode->current.enabled && g_maxActors > MAX_ACTORS_RETAIL ? (g_modSentientCopyDvar->current.integer ? g_modSentientCopyDvar->current.integer : svFast) : 0; }
    g_entNumBits = g_maxActors > MAX_ACTORS_RETAIL && g_modNetEnts->current.integer ? 11 : 10;
    g_entNumCscEnd = g_entNumBits > 10 && g_modNetEnts->current.integer >= 2 ? 0x500 : ENTNUM_CSC_END; // mod (L55): q_shared.h
    g_scrEntNumLimit = g_maxActors > MAX_ACTORS_RETAIL ? MAX_GENTITIES_SV : MAX_GENTITIES; // retail 1024
    { void MSG_SetEntityNumBits(); MSG_SetEntityNumBits(); } // mod (L25): GENTITYNUM_BITS netfields
    if (g_maxActors != MAX_ACTORS_RETAIL)
        Com_Printf(15, "mod: bo1_mod_maxactors %d (sentients %d)\n", g_maxActors, g_maxSentients);
    EntHandle::Init();
    G_SP_ResetAnimCommands();
    G_SP_ResetActorModels();
    SentientHandle::Init();
    memset((unsigned __int8 *)&level, 0, sizeof(level));
    G_SP_ClearLevelBuiltinState();
    G_SP_ResetClientConnects();
    // zombies: reset entity builtin side tables before scripts (SP G_InitGame 0x0051E8F0).
    if (G_SP_IsSPLevel())
    {
        G_SP_ResetEntityBuiltins();
        Path_SP_ResetLinkOverrides(restart); // zombies: linknodes/unlinknodes records (SP G_InitGame -> 0x004587d0)
    }
    level.initializing = 1;
    level.currentEntityThink = -1;
    level.scriptPrintChannel = 25;
    srand(randomSeed);
    Rand_Init(randomSeed);
    if ( registerDvars )
        G_RegisterDvars();
    G_SP_RegisterLevelDvars();
    // zombies: V10 register before _gameskill can read it, not just in getdifficulty
    // (SP 0x00698265: default 1, range 0..3, flags 0x1064 including LATCH).
    const dvar_s *gameskill = nullptr;
    if (G_SP_IsSPLevel())
        gameskill = _Dvar_RegisterInt("g_gameskill", 1, 0, 3, 0x1064, "Game difficulty");
    // zombies: SP G_InitGame's not-a-restart block (SP 0x0051E972..0x0051E9BF): keep g_gameskill
    // (SP 0x0051E977), reset every 0x1000 dvar (SP 0x0051E98A -> Dvar_ResetDvars 0x005A6430, source 0),
    // cheat state, skill dvars, then put g_gameskill back (SP 0x0051E9B7). The reset is what clears
    // hud_missionFailed (0x1000, set by missionfailed / CodeCallback_PlayerKilled) for the next level:
    // without it the reload after game over kept the zombie HUD (weaponinfo_zombie, dpad_zombie,
    // competitivemodescores all test hud_missionFailed == 0) hidden. Not ported: 0x005AC520 (configstring
    // clear; KB's SV_SpawnServer clears them).
    int savedGameskill = 0;
    if ( gameskill && !restart )
    {
        savedGameskill = gameskill->current.integer;
        Dvar_ResetDvars(0x1000u, DVAR_SOURCE_INTERNAL);
    }
    if ( !Dvar_GetBool("sv_cheats") && !restart )
        Dvar_SetCheatState();
    // zombies: the skill-dvar update after the cheat state (SP 0x0051E9A5 -> 0x00640590):
    // g_player_maxhealth 100, player_damageMultiplier 100 / player_health<skill>, player_deathInvulnerableTime.
    if ( G_SP_IsSPLevel() && !restart )
        G_SP_UpdateSkillDvars();
    if ( gameskill && !restart )
        Dvar_SetInt(const_cast<dvar_s *>(gameskill), savedGameskill);
    GScr_LoadConsts();
    // zombies: SP 0x0051E9EA..0x0051EA17. KB keeps the MP arguments; SP's argument 3 is
    // mapChecksum, not randomSeed. This path supports the same-map, retained-script memory save.
    const bool loadingSave = G_SP_IsSPLevel() && loadGame;
    if (save)
        *save = nullptr;
    if (loadingSave)
    {
        iassert(restart && save);
        *save = G_SP_PrepareLoadGame(g_spSaveMapChecksum);
    }

    // === IDA OMITTED BELOW THIS === 
    // (to get around it, I undefined the code and just created a new function where it was stopping)
    G_SetupWeaponDef();
    G_ProcessIPBans();
    UI_Gametype_CustomGameModeDataToDvars();

    level_bgs.animData = &level_bgsAnim;
    level_bgs.GetXModel = SV_XModelGet;
    level_bgs.CreateDObj = G_CreateDObj;
    level_bgs.AttachWeapon = G_AttachWeapon;
    level_bgs.GetDObj = G_GetDObj;
    level_bgs.SafeDObjFree = G_SafeDObjFree;
    level_bgs.AllocXAnim = Hunk_AllocXAnimServer;
    level_bgs.anim_user = 1;
    level_bgs.Rand = G_rand;
    level_bgs.Random = G_random;
    level_bgsAnim.done_notify = scr_const.done;

    G_InitDestructibles();
    CM_LinkWorld();

    level.time = levelTime;
    level.startTime = levelTime;
    G_SP_MeasureInit();
    G_SP_PlayTraceRegister(); // j1: play-session recorder dvars (tool, off by default)
    SND_BO1TraceInit(); // zombies (a1): bo1_audio, trace only
    SND_BO1TraceBanks("G_InitGame");

    G_srand(randomSeed);
    G_Mapkit_ResetChecks(); // per map/restart, even when g_log is disabled
    // zombies: SP 0x0051EA9F..0x0051EB1B, before the script checksum and entity initialization.
    if (loadingSave)
    {
        MemoryFile *memFile = &(*save)->memFile[0];
        MemFile_MoveToSegment(memFile, 1);
        if (!G_SP_LoadWeapons(memFile))
        {
            // The reset-and-retry branch (SP 0x0051EACF) still needs its weapon-memory mark
            // helpers. Same-process retained weapons must already have the saved indices.
            Com_Error(ERR_DROP, "Failed to load weapons.\n");
        }
        G_SP_LoadRegisteredWeapons(memFile);
        Com_Printf(15, "loadgame: G_InitGame loaded saved weapons\n");
    }

    if (*(_BYTE *)g_log->current.integer)
    {
        if (g_logSync->current.enabled)
            FS_FOpenFileByMode((char *)g_log->current.integer, &level.logFile, FS_APPEND_SYNC);
        else
            FS_FOpenFileByMode((char *)g_log->current.integer, &level.logFile, FS_APPEND);
        if (level.logFile)
        {
            char serverinfo[1024];
            SV_GetServerinfo(serverinfo, sizeof(serverinfo));
            G_LogPrintf("------------------------------------------------------------\n");
            G_LogPrintf("InitGame: %s\n", serverinfo);
            G_LogPrintf(
                "InitGame: %s\\g_logTimeStampInSeconds\\%d\n",
                serverinfo,
                g_logTimeStampInSeconds->current.color[0] != 0);
        }
        else
        {
            Com_PrintWarning(15, "WARNING: Couldn't open logfile: %s\n", g_log->current.string);
        }
    }
    else
    {
        Com_Printf(15, "Not logging to disk.\n");
    }

    for (int i = 0; i < 1; i++)
    {
        level.openScriptIOFileHandles[i] = 0;
        level.openScriptIOFileBuffers[i] = 0;
        memset(&level.currentScriptIOLineMark[i], 0, sizeof(level.currentScriptIOLineMark[i]));
    }

    Mantle_CreateAnims(Hunk_AllocXAnimServer);
    level.actorCorpseCount = ai_corpseCount->current.integer;
    // zombies: SP G_InitGame (SP 0x0051E8F0) takes ai_corpseCount (0..32) with no check, then the zombietron
    // override (ai_zombietronCorpseCount when zombietron and > 0).
    if (G_SP_IsSPLevel())
    {
        const int zombietronCorpseCount = Dvar_GetInt("ai_zombietronCorpseCount");
        if (Dvar_GetBool("zombietron") && zombietronCorpseCount > 0)
            level.actorCorpseCount = zombietronCorpseCount;
    }
    else if ((level.actorCorpseCount < 1 || level.actorCorpseCount > 8)
        && !Assert_MyHandler(
            "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
            2159,
            1,
            "%s",
            "level.actorCorpseCount >= 1 && level.actorCorpseCount <= MAX_ACTOR_CORPSES"))
    {
        __debugbreak();
    }

    // zombies: SP's G_InitGame has no Dog_CreateAnims (SP 0x0051E8F0); the MP dog xanims are not in its zones
    if (!G_SP_IsSPLevel())
        Dog_CreateAnims(Hunk_AllocXAnimServer);
    SpawnSystem_Init();

    VehAnim_Init();

    if (restart)
        ; //BG_EvalVehicleName();
    else
        G_ParseScrVehicleInfo();

    iassert(bgs == 0);
    bgs = &level_bgs;

    if (!restart)
    {
        memset(&bgs->animData->animScriptData, 0, sizeof(animScriptData_t)/*0x8D388u*/);
        static_assert(sizeof(animScriptData_t) == 0x8D388);

        bgs->animData->animScriptData.soundAlias = SND_FindAlias;
        bgs->animData->animScriptData.playSoundAlias = G_AnimScriptSound;
        //*(_DWORD *)(a1 - 2076) = Dvar_GetString("mapname");

        {
            PROF_SCOPED("GScr_LoadScripts");
            if (!G_ExitAfterToolComplete())
                DB_SyncXAssets();

            GScr_LoadScripts(SCRIPTINSTANCE_SERVER);
        }
        
        // IDA being annoying here too
        const char *mapname = Dvar_GetString("mapname");
        BG_LoadAnim(mapname);
        // zombies: SP finds the actors' animtree next (SP G_LoadScripts 0x007E2F20 -> 0x005687F0)
        if (G_SP_IsSPLevel())
            G_SP_FindActorAnimTree();
        BG_PostLoadAnim(mapname);
        G_LoadAnimTreeInstances();
    }

    // zombies: SP 0x0051EC5B checks the three checksum words even when header +0x10 is false.
    if (loadingSave && !G_SP_SaveScriptChecksumMatches(*save))
    {
        bgs = NULL;
        Dvar_SetStringByName("error_menu_info", "EXE_SCRIPTS_OUT_OF_DATE");
        Com_Error(ERR_DROP, "EXE_SCRIPTS_OUT_OF_DATE");
    }

    char buffer[1024];
    SV_GetConfigstring(0x15u, buffer, 1024);
    Info_SetValueForKey(buffer, "winner", "0");
    SV_SetConfigstring(21, buffer);

    memset(g_entities, 0, sizeof(g_entities));
    // zombies: SP G_InitGame (SP 0x0051E8F0) stores 1 in every gentity's s.lerp.useCount (gentity+0x74) right
    // after this memset, so a slot's first entity reaches the client with a useCount the client's cleared
    // centity (0) does not have and CG_ResetEntity replays its event ring (SP 0x00897B40 tail). That is how SP
    // shows playfxontag fx of script models spawned before the client's first snapshot (box beam, level-start
    // mover fx). Measured in SP: session3-manual memdump-88450, every never-used slot 814..1021 has useCount 1.
    if (G_SP_IsSPLevel())
    {
        for (int i = 0; i < ARRAY_COUNT(g_entities); ++i)
            g_entities[i].s.lerp.useCount = 1;
    }
    level.gentities = g_entities;
    g_numEntitiesNetHi = 0; // mod (L25): networked entity numbers 1537+
    g_numEntitiesHi = 0; // mod (L25): server-only entity numbers 1024+
    g_entities[1023].flags |= 0x4000000u;
    level.maxclients = com_maxclients->current.integer;
    memset(g_clients, 0, sizeof(g_clients));
    level.clients = g_clients;
    memset(g_timed_radius_damage, 0, sizeof(g_timed_radius_damage));

    Path_Init(restart);

    level.sentients = g_sentients;
    level.actors = g_actors;
    G_InitSentients();
    G_InitActors();

    for (int i = 0; i < level.maxclients; ++i)
        g_entities[i].client = &level.clients[i];

    // zombies: 44 on MP; an SP level reserves 32 actor corpse clones (Com_GetFirstFreeEntityNum)
    level.num_entities = Com_GetFirstFreeEntityNum();
    level.firstFreeEnt = 0;
    level.lastFreeEnt = 0;
    SV_LocateGameData(level.gentities, level.num_entities, 760, &level.clients->ps, 10720);

    G_ParseHitLocDmgTable();
    BG_LoadPenetrationDepthTable();
    G_InitVehiclePaths();
    G_InitScrVehicles();
    G_InitTurrets();
    GlassSv_Init();
    DynEntSv_InitEntities();
    Path_PreSpawnInitPaths();

    // zombies: SP G_InitGame (0x0051E8F0) drops the pathnodes once, after the map entities are spawned
    // (G_DropPathnodesToFloor 0x0060DCB0, the "if (!restart)" call below). Dropping before the spawn traced
    // through brushmodels that do not exist yet: Five's elevator cab nodes (elevator1 *633 has no DYNAMICPATH
    // spawnflag, so SP keeps it solid) fell down the shaft (-512 -> -716) or were marked BAD NODE "floating"
    // (elevator2), and zombies could not path to a player riding either elevator.
    if (!restart && !G_SP_IsSPLevel())
        G_DropPathnodesToFloor();

    G_SpawnEntitiesFromString();
    if (G_SP_IsSPLevel())
        G_SP_InitGravityVolumes(); // zombies: SP builds the zero gravity volumes at level load (SP 0x00561830)
    G_setfog((char*)"0");

    if (!restart && !IsFastFileLoad())
    {
        G_DropPathnodesToFloor();
    }

    G_SetupVehiclePaths();
    G_SetupScrVehicles();
    G_InitObjectives();
    Missile_InitAttractors();
    Path_PreSpawnInitPaths();

    if (!restart)
        G_Mapkit_AppendPathnodes(); // mod: mapkit (bo1_mod_mapkit, off = no-op)
    else
        G_Mapkit_RestorePathnodeStrings(); // mod: mapkit (off = no-op)

    if (!restart)
        G_DropPathnodesToFloor();

    G_UpdateTrackExtraNodes();

    if (!restart)
        G_Mapkit_LinkPathnodes(); // mod: mapkit (off = no-op)

    if (!restart && !IsFastFileLoad())
    {
        G_DropPathnodesToFloor();
    }

    G_DropActorSpawnersToFloor();
    Scr_FreeEntityList(SCRIPTINSTANCE_SERVER);
    Com_Printf(15, "-----------------------------------\n");
    G_InitTargets();
    // zombies: SP 0x0051EEA6 skips this entire startup block for loading state 2. The VM
    // reader will create the saved roots and threads; do not run Five's main a second time.
    if (!loadingSave)
    {
        Scr_InitSystem(SCRIPTINSTANCE_SERVER, 1);
        Scr_SetLoading(1, SCRIPTINSTANCE_SERVER);
        Scr_AllocGameVariable(SCRIPTINSTANCE_SERVER);

        {
            PROF_SCOPED("Load Scripts");

            if (g_loadScripts && g_loadScripts->current.enabled)
            {
                {
                    PROF_SCOPED("Load Structs");
                    G_LoadStructs();
                }

                if (!r_reflectionProbeGenerate->current.enabled)
                {
                    Path_InitPaths();
                    // zombies: aitype spawner / precache functions (SP 0x0049CC70)
                    if (G_SP_IsSPLevel())
                        G_SP_RunAITypeLoadScripts();
                    Actor_FinishSpawningAll();
                    Path_AutoDisconnectPaths();
                }

                if (G_SP_IsSPLevel())
                {
                    // zombies: SP runs the level main here, at level load (Scr_StartLevelMain 0x004B7F80). It has no
                    // gametype main, no pregame and never calls CodeCallback_StartGameType.
                    PROF_SCOPED("Load Level");
                    Scr_SP_StartLevelMain();
                }
                else
                {
                {
                    PROF_SCOPED("Load Game");
                    if (Pregame_ShouldLoadPregame())
                    {
                        Pregame_StartPregame();
                        Scr_LoadPreGame();
                    }
                    else
                    {
                        Scr_LoadGameType();
                    }
                }

                {
                    PROF_SCOPED("Load Level");
                    Scr_LoadLevel();
                }

                {
                    PROF_SCOPED("Startup Gametype");
                    Scr_StartupGameType();
                }
                }
            }
        }

    }

    // Not ported: segment-1 configstring restore (SP 0x0051EF4D -> 0x0048ACD0).
    // The partial restore diagnostic always drops before any server frame can run.
    for (int i = 0; i < 4; i++)
    {
        g_scr_data.playerCorpseInfo[i].entnum = -1;
    }

    if (IsFastFileLoad())
    {
        G_PrintAllFastFileErrors();
    }

    iassert(bgs == &level_bgs);
    bgs = NULL;

    g_timed_radius_damage_count = 0;
    level.initializing = 0;
    SaveRegisteredWeapons();
    SaveRegisteredItems();

    if (!restart)
        RadiantRemoteInit();

    RunSavedRadiantCmds();

    //result = GetCurrentThreadId();
    //*(_DWORD *)(a1 - 2140) = result;
    //*(_DWORD *)(a1 - 2136) = 0;
    //if (*(_DWORD *)(a1 - 2140) == (_DWORD)g_DXDeviceThread)
    //{
    //    result = *(_DWORD *)(a1 - 2136);
    //    if (result == HIDWORD(g_DXDeviceThread))
    //        return D3DPERF_EndEvent();
    //}
    //return result;
}

void G_RegisterDvars()
{
    const dvar_s *result; // eax

    g_cheats = _Dvar_RegisterBool("sv_cheats", 1, 0, "Enable cheats");
    g_erroronpathsnotconnected = _Dvar_RegisterBool(
                                                                 "g_erroronpathsnotconnected",
                                                                 1,
                                                                 0x80u,
                                                                 "Errors out during load if paths are not connected.");
    //sv_mapname = _Dvar_RegisterString("sv_mapname", (char *)"", 0x44u, "The current map name");
    _Dvar_RegisterString("sv_mapname", (char *)"", 0x44u, "The current map name");
    g_gametype = _Dvar_RegisterString("g_gametype", "tdm", 0x24u, "The current campaign");
    g_synchronousClients = _Dvar_RegisterBool(
                                                     "g_synchronousClients",
                                                     0,
                                                     0x100u,
                                                     "Call 'client think' exactly once for each server frame to make smooth demos");
    g_log = _Dvar_RegisterString("g_log", "games_mp.log", 1u, "Log file name");
    g_logTimeStampInSeconds = _Dvar_RegisterBool(
                                                            "g_logTimeStampInSeconds",
                                                            0,
                                                            1u,
                                                            "Enable logging with time stamps in seconds since UTC 1/1/1970");
    g_logSync = _Dvar_RegisterBool("g_logSync", 0, 1u, "Enable synchronous logging");
    g_password = _Dvar_RegisterString("g_password", (char *)"", 0, "Password");
    g_banIPs = _Dvar_RegisterString("g_banIPs", (char *)"", 1u, "IP addresses to ban from playing");
    g_speed = _Dvar_RegisterInt("g_speed", 190, 0x80000000, 0x7FFFFFFF, 0, "Player speed");
    g_knockback = _Dvar_RegisterFloat("g_knockback", 1000.0, -3.4028235e38, 3.4028235e38, 0, "Maximum knockback");
    g_maxDroppedWeapons = _Dvar_RegisterInt("g_maxDroppedWeapons", 16, 2, 32, 0, "Maximum number of dropped weapons");
    g_inactivity = _Dvar_RegisterInt(
                                     "g_inactivity",
                                     0,
                                     0,
                                     0x7FFFFFFF,
                                     0,
                                     "Time delay before player is kicked for inactivity");
    g_debugDamage = _Dvar_RegisterBool("g_debugDamage", 0, 0x80u, "Show debug information for damage");
    g_debugBullets = _Dvar_RegisterInt("g_debugBullets", 0, -3, 6, 0x80u, "Show debug information for bullets");
    g_vehicleDrawPath = _Dvar_RegisterString(
                                                "g_vehicleDrawPath",
                                                (char *)"",
                                                0x80u,
                                                "Turn on debug information for vehicle paths");
    ai_enableBadPlaces = _Dvar_RegisterBool("ai_enableBadPlaces", 1, 0, "toggle badplaces system on/off");
    g_ai = _Dvar_RegisterBool("g_ai", 1, 0x2080u, "Enable AI");
    g_spawnai = _Dvar_RegisterBool("g_spawnai", 1, 0x20A0u, "Enable AI spawning");
    g_dumpAIEvents = _Dvar_RegisterInt("g_aiEventDump", -1, -1, 1023, 0x80u, "Print AI events happening for this entity");
    ai_turnRate = _Dvar_RegisterFloat("ai_turnRate", 0.30000001, 0.0099999998, 0.5, 0x4080u, "turn rate for AI");
    // zombies: SP AI_RegisterDvars_A registers it (SP 0x007E1AA6: 0.2 in [0, 1], cheat, no description);
    // zombie_pentagon_teleporter reads GetDvarFloat("g_banzai_player_fov_buffer") (KB lacked it: 0).
    _Dvar_RegisterFloat("g_banzai_player_fov_buffer", 0.2f, 0.0f, 1.0f, 0x80u, "");
    ai_useFacingTranslation = _Dvar_RegisterBool(
                                                            "ai_useFacingTranslation",
                                                            0,
                                                            0x4080u,
                                                            "whether to use facing to determine direction of translation");
    ai_useLeanRunAnimations = _Dvar_RegisterBool(
                                                            "ai_useLeanRunAnimations",
                                                            0,
                                                            0x4080u,
                                                            "whether to use lean run animations instead of strafes");
    ai_useBetterLookahead = _Dvar_RegisterBool("ai_useBetterLookahead", 1, 0x4080u, "t5 lookahead improvements");
    ai_slowdownMinYawDiff = _Dvar_RegisterFloat(
                                                        "ai_slowdownMinYawDiff",
                                                        0.0,
                                                        0.0,
                                                        180.0,
                                                        0x4080u,
                                                        "min yaw diff before slowdown kicks in");
    ai_slowdownMaxYawDiff = _Dvar_RegisterFloat(
                                                        "ai_slowdownMaxYawDiff",
                                                        90.0,
                                                        0.0,
                                                        180.0,
                                                        0x4080u,
                                                        "max yaw diff used for slowdown cal");
    ai_slowdownMinRate = _Dvar_RegisterFloat(
                                                 "ai_slowdownMinRate",
                                                 1.0,
                                                 0.0,
                                                 1.0,
                                                 0x4080u,
                                                 "anim rate at ai_slowdownMaxYawDiff");
    ai_slowdownRateBlendFactor = _Dvar_RegisterFloat(
                                                                 "ai_slowdownRateBlendFactor",
                                                                 1.0,
                                                                 0.0,
                                                                 1.0,
                                                                 0x4080u,
                                                                 "percent of desired rate that goes into final rate");
    ai_angularYawEnabled = _Dvar_RegisterBool("ai_angularYawEnabled", 0, 0x4080u, "turn on velocity based body rotation");
    ai_angularYawAccelRate = _Dvar_RegisterFloat(
                                                         "ai_angularYawAccelRate",
                                                         1200.0,
                                                         0.0,
                                                         5000.0,
                                                         0x4080u,
                                                         "yaw acceleration rate");
    ai_angularYawDecelFactor = _Dvar_RegisterFloat(
                                                             "ai_angularYawDecelFactor",
                                                             1.0,
                                                             0.0,
                                                             5.0,
                                                             0x4080u,
                                                             "yaw deceleration factor (decel rate = factor * accel rate)");
    // zombies: SP registers 5, 0..32, 0x2001 (SP 0x007E0F9D); MP 0..8. G_SP_RegisterLevelDvars keeps the domain
    // in step when the level kind changes in one process.
    ai_corpseCount = _Dvar_RegisterInt("ai_corpseCount", 5, 0, Com_GetMaxActorCorpses(), 0x2001u, "Maximum number of AI corpses");
    ai_showNodes = _Dvar_RegisterInt("ai_showNodes", 0, 0, 4, 0x80u, "Show AI navigation node debug information");
    ai_showNodesDist = _Dvar_RegisterFloat(
                                             "ai_showNodesDist",
                                             384.0,
                                             0.0,
                                             3.4028235e38,
                                             0x80u,
                                             "Maximum distance from the camera at which AI nodes are shown");
    ai_showNearestNode = _Dvar_RegisterInt("ai_showNearestNode", 0, 0, 64, 0x80u, "Show nodes closest to AI");
    ai_showVisData = _Dvar_RegisterInt("ai_showVisData", 0, 0, 2, 0x80u, "Display debug information for visibility data");
    ai_showVisDataDist = _Dvar_RegisterFloat(
                                                 "ai_showVisDataDist",
                                                 1000.0,
                                                 0.0,
                                                 3.4028235e38,
                                                 0x80u,
                                                 "Maximum distance for visibility data debugging information to be shown");
    ai_showPaths = _Dvar_RegisterInt("ai_showPaths", 0, 0, 2, 0x80u, "Show AI navigation paths");
    ai_debugFindPath = _Dvar_RegisterInt(
                                             "ai_debugFindPath",
                                             0,
                                             0,
                                             5,
                                             0x80u,
                                             "Display AI 'find path' debugging information");
    ai_debugFindPathDirect = _Dvar_RegisterBool(
                                                         "ai_debugFindPathDirect",
                                                         0,
                                                         0x80u,
                                                         "Display AI 'find direct path' debugging information");
    ai_debugFindPathWidth = _Dvar_RegisterFloat(
                                                        "ai_debugFindPathWidth",
                                                        0.0,
                                                        -3.4028235e38,
                                                        3.4028235e38,
                                                        0x80u,
                                                        "Display paths with the given width");
    ai_debugFindPathLock = _Dvar_RegisterBool("ai_debugFindPathLock", 0, 0x80u, "Find path lock");
    ai_debugClaimedNodes = _Dvar_RegisterInt(
                                                     "ai_debugClaimedNodes",
                                                     0,
                                                     0,
                                                     64,
                                                     0x80u,
                                                     "Enable debugging information claimed status of nodes");
    ai_disableSpawn = _Dvar_RegisterBool("ai_disableSpawn", 0, 0x2080u, "Do not spawn AI");
    ai_moveOrientMode = _Dvar_RegisterEnum("ai_moveOrientMode", moveOrientModeStrings, 0, 0x80u, "Debug AI Orient Mode");
    ai_pathNegotiationOverlapCost = _Dvar_RegisterFloat(
                                                                        "ai_pathNegotiationOverlapCost",
                                                                        300.0,
                                                                        0.0,
                                                                        1000.0,
                                                                        0x2080u,
                                                                        "The distance AI would travel around to avoid going to a negotiation being used. Mult"
                                                                        "iplied by number of users of the negotiation");
    ai_showPotentialThreatDir = _Dvar_RegisterBool(
                                                                "ai_showPotentialThreatDir",
                                                                0,
                                                                0x80u,
                                                                "Display AI potential threat direction");
    ai_debugCoverEntityNum = _Dvar_RegisterInt(
                                                         "ai_debugCoverEntityNum",
                                                         -1,
                                                         -1,
                                                         1023,
                                                         0x80u,
                                                         "Display debug info for cover");
    ai_showBadPlaces = _Dvar_RegisterBool("ai_showBadPlaces", 0, 0x80u, "Display debug information for 'bad places'");
    ai_showDodge = _Dvar_RegisterBool("ai_showDodge", 0, 0x80u, "Display debug information for AI dodging");
    ai_noDodge = _Dvar_RegisterBool("ai_noDodge", 0, 0x2080u, "AI won't dodge to the side");
    ai_pathMomentum = _Dvar_RegisterFloat(
                                            "ai_pathMomentum",
                                            0.77999997,
                                            0.0,
                                            0.89999998,
                                            0x2080u,
                                            "Momentum factor for continuing motion in previous direction. 0 for no momentum carry over");
    ai_debugMayMove = _Dvar_RegisterBool(
                                            "ai_debugMayMove",
                                            0,
                                            0x80u,
                                            "Display debug information for AI 'may move' calculations");
    ai_showVolume = _Dvar_RegisterInt(
                                        "ai_showVolume",
                                        -1,
                                        -1,
                                        1023,
                                        0x80u,
                                        "Draw the goal volume and fixed node safe volume for an AI");
    ai_debugAnimDeltas = _Dvar_RegisterInt(
                                                 "ai_debugAnimDeltas",
                                                 0,
                                                 0,
                                                 1023,
                                                 0x80u,
                                                 "Display animation delta debug information");
    ai_debugThreatSelection = _Dvar_RegisterBool(
                                                            "ai_debugThreatSelection",
                                                            0,
                                                            0x81u,
                                                            "Enable debugging information for threat selection");
    ai_debugMeleeAttackSpots = _Dvar_RegisterBool(
                                                             "ai_debugMeleeAttackSpots",
                                                             0,
                                                             0x81u,
                                                             "Enable debugging information for melee attack spots");
    ai_debugEntIndex = _Dvar_RegisterInt("ai_debugEntIndex", -1, -1, 1023, 0, "Entity index of an entity to debug");
    ai_eventDistFootstep = _Dvar_RegisterFloat(
                                                     "ai_eventDistFootstep",
                                                     512.0,
                                                     0.0,
                                                     30000.0,
                                                     0x3080u,
                                                     "Distance used for AI event");
    ai_eventDistFootstepLite = _Dvar_RegisterFloat(
                                                             "ai_eventDistFootstepLite",
                                                             256.0,
                                                             0.0,
                                                             30000.0,
                                                             0x3080u,
                                                             "Distance used for AI event");
    ai_eventDistNewEnemy = _Dvar_RegisterFloat(
                                                     "ai_eventDistNewEnemy",
                                                     1024.0,
                                                     0.0,
                                                     30000.0,
                                                     0x3080u,
                                                     "Distance used for AI event");
    ai_eventDistReact = _Dvar_RegisterFloat(
                                                "ai_eventDistReact",
                                                512.0,
                                                0.0,
                                                3.4028235e38,
                                                0x3080u,
                                                "Distance used for AI event");
    ai_eventDistPain = _Dvar_RegisterFloat("ai_eventDistPain", 512.0, 0.0, 30000.0, 0x3080u, "Distance used for AI event");
    ai_eventDistDeath = _Dvar_RegisterFloat(
                                                "ai_eventDistDeath",
                                                1024.0,
                                                0.0,
                                                30000.0,
                                                0x3080u,
                                                "Distance used for AI event");
    ai_eventDistExplosion = _Dvar_RegisterFloat(
                                                        "ai_eventDistExplosion",
                                                        1024.0,
                                                        0.0,
                                                        30000.0,
                                                        0x3080u,
                                                        "Distance used for AI event");
    ai_eventDistGrenadePing = _Dvar_RegisterFloat(
                                                            "ai_eventDistGrenadePing",
                                                            512.0,
                                                            0.0,
                                                            30000.0,
                                                            0x3080u,
                                                            "Distance used for AI event");
    ai_eventDistProjPing = _Dvar_RegisterFloat(
                                                     "ai_eventDistProjPing",
                                                     128.0,
                                                     0.0,
                                                     30000.0,
                                                     0x3080u,
                                                     "Distance used for AI event");
    ai_eventDistGunShot = _Dvar_RegisterFloat(
                                                    "ai_eventDistGunShot",
                                                    2048.0,
                                                    0.0,
                                                    30000.0,
                                                    0x3080u,
                                                    "Distance used for AI event");
    ai_eventDistSilencedShot = _Dvar_RegisterFloat(
                                                             "ai_eventDistSilencedShot",
                                                             128.0,
                                                             0.0,
                                                             30000.0,
                                                             0x3080u,
                                                             "Distance used for AI event");
    ai_eventDistBullet = _Dvar_RegisterFloat(
                                                 "ai_eventDistBullet",
                                                 96.0,
                                                 0.0,
                                                 30000.0,
                                                 0x3080u,
                                                 "Distance used for AI event");
    ai_eventDistBulletRunning = _Dvar_RegisterFloat(
                                                                "ai_eventDistBulletRunning",
                                                                200.0,
                                                                0.0,
                                                                3.4028235e38,
                                                                0x3080u,
                                                                "Distance used for AI event");
    ai_eventDistProjImpact = _Dvar_RegisterFloat(
                                                         "ai_eventDistProjImpact",
                                                         BG_ZombieFloatDefault("ai_eventDistProjImpact", 512.0f, 256.0f), // zombies: SP 0x007E24C2
                                                         0.0,
                                                         30000.0,
                                                         0x3080u,
                                                         "Distance used for AI event");
    ai_eventDistBadPlace = _Dvar_RegisterFloat(
                                                     "ai_eventDistBadPlace",
                                                     256.0,
                                                     0.0,
                                                     30000.0,
                                                     0x3080u,
                                                     "Distance used for AI event");
    ai_playerNearAccuracy = _Dvar_RegisterFloat(
                                                        "ai_playerNearAccuracy",
                                                        0.5,
                                                        0.0,
                                                        1.0,
                                                        0x2000u,
                                                        "Accuracy for an AI near to a player");
    ai_playerNearRange = _Dvar_RegisterFloat(
                                                 "ai_playerNearRange",
                                                 800.0,
                                                 0.0,
                                                 3.4028235e38,
                                                 0x2000u,
                                                 "Maximum range for AI to use 'near' accuracy");
    ai_playerFarAccuracy = _Dvar_RegisterFloat(
                                                     "ai_playerFarAccuracy",
                                                     0.1,
                                                     0.0,
                                                     1.0,
                                                     0x2000u,
                                                     "Accuracy for AI far away from the player");
    ai_playerFarRange = _Dvar_RegisterFloat(
                                                "ai_playerFarRange",
                                                2000.0,
                                                0.0,
                                                3.4028235e38,
                                                0x2000u,
                                                "Minimum range for AI to use 'far' accuracy");
    ai_threatUpdateInterval = _Dvar_RegisterInt(
                                                            "ai_threatUpdateInterval",
                                                            500,
                                                            0,
                                                            0x7FFFFFFF,
                                                            0x2080u,
                                                            "AI target threat update interval in milliseconds");
    ai_foliageIngoreDist = _Dvar_RegisterFloat(
                                                     "ai_foliageSeeThroughDist",
                                                     128.0,
                                                     0.0,
                                                     3.4028235e38,
                                                     0x3080u,
                                                     "Maximum distance AI ignore foliage for sight trace to targets");
    ai_friendlySuppression = _Dvar_RegisterBool(
                                                         "ai_friendlySuppression",
                                                         1,
                                                         0x3080u,
                                                         "Whether AI fire will suppression teammates or not.");
    ai_friendlySuppressionDist = _Dvar_RegisterFloat(
                                                                 "ai_friendlySuppressionDist",
                                                                 128.0,
                                                                 0.0,
                                                                 2048.0,
                                                                 0x3080u,
                                                                 "Max distance at which AI suppress teammates");
    ai_meleeRange = _Dvar_RegisterFloat(
                                        "ai_meleeRange",
                                        64.0,
                                        0.0,
                                        1000.0,
                                        0x80u,
                                        "The maximum range of the AI's melee attack");
    ai_meleeWidth = _Dvar_RegisterFloat("ai_meleeWidth", 20.0, 0.0, 1000.0, 0x80u, "The width of the AI's melee attack");
    ai_meleeHeight = _Dvar_RegisterFloat(
                                         "ai_meleeHeight",
                                         10.0,
                                         0.0,
                                         1000.0,
                                         0x80u,
                                         "The height of the AI's melee attack");
    ai_meleeDamage = _Dvar_RegisterInt(
                                         "ai_meleeDamage",
                                         100,
                                         0,
                                         1000,
                                         0x80u,
                                         "The amount of damage dealt by AI's melee attack");
    ai_maxAttackerCount = _Dvar_RegisterInt(
                                                    "ai_maxAttackerCount",
                                                    2,
                                                    0,
                                                    1000,
                                                    0x80u,
                                                    "Max number of AI's that will attack one player");
    ai_noPathToEnemyGiveupTime = _Dvar_RegisterInt(
                                                                 "ai_noPathToEnemyGiveupTime",
                                                                 6000,
                                                                 0,
                                                                 60000,
                                                                 0x80u,
                                                                 "Time the AI will continue to attack if the player goes off the path grid.");
    bullet_penetrationEnabled = _Dvar_RegisterBool(
                                                                "bullet_penetrationEnabled",
                                                                1,
                                                                0x80u,
                                                                "Enable/Disable bullet penetration.");
    g_entinfo = _Dvar_RegisterEnum("g_entinfo", g_entinfoNames, 0, 0x80u, "Display entity information");
    g_entinfo_type = _Dvar_RegisterEnum(
                                         "g_entinfo_type",
                                         g_entinfoTypeNames,
                                         1,
                                         0x80u,
                                         "Type of entities to display information");
    g_entinfo_AItext = _Dvar_RegisterEnum(
                                             "g_entinfo_AItext",
                                             g_entinfoAITextNames,
                                             1,
                                             0x80u,
                                             "Type of text information for AI entinfo");
    g_entinfo_maxdist = _Dvar_RegisterFloat(
                                                "g_entinfo_maxdist",
                                                2048.0,
                                                0.0,
                                                3.4028235e38,
                                                0x80u,
                                                "Maximum distance of an entity from the camera at which to show entity information");
    g_entinfo_scale = _Dvar_RegisterFloat(
                                            "g_entinfo_scale",
                                            1.0,
                                            0.0,
                                            10.0,
                                            0x80u,
                                            "Scale of the entity information text");
    g_debugPlayerAnimScript = _Dvar_RegisterInt(
                                                            "g_debugPlayerAnimScript",
                                                            -1,
                                                            -1,
                                                            32,
                                                            0,
                                                            "Show debug information for playeranim.script");
    g_motd = _Dvar_RegisterString("g_motd", (char *)"", 0, "The message of the day");
    g_playerCollisionEjectSpeed = _Dvar_RegisterInt(
                                                                    "g_playerCollisionEjectSpeed",
                                                                    25,
                                                                    0,
                                                                    32000,
                                                                    1u,
                                                                    "Speed at which to push intersecting players away from each other");
    g_dropForwardSpeed = _Dvar_RegisterFloat(
                                                 "g_dropForwardSpeed",
                                                 10.0,
                                                 0.0,
                                                 1000.0,
                                                 1u,
                                                 "Forward speed of a dropped item");
    g_dropUpSpeedBase = _Dvar_RegisterFloat(
                                                "g_dropUpSpeedBase",
                                                10.0,
                                                0.0,
                                                1000.0,
                                                1u,
                                                "Base component of the initial vertical speed of a dropped item");
    g_dropUpSpeedRand = _Dvar_RegisterFloat(
                                                "g_dropUpSpeedRand",
                                                5.0,
                                                0.0,
                                                1000.0,
                                                1u,
                                                "Random component of the initial vertical speed of a dropped item");
    g_dropHorzSpeedRand = _Dvar_RegisterFloat(
                                                    "g_dropHorzSpeedRand",
                                                    100.0,
                                                    0.0,
                                                    1000.0,
                                                    1u,
                                                    "Random component of the initial horizontal speed of a dropped item");
    g_clonePlayerMaxVelocity = _Dvar_RegisterFloat(
                                                             "g_clonePlayerMaxVelocity",
                                                             80.0,
                                                             0.0,
                                                             3.4028235e38,
                                                             1u,
                                                             "Maximum velocity in each axis of a cloned player\n(for death animations)");
    voice_global = _Dvar_RegisterBool("voice_global", 0, 1u, "Send voice messages to everybody");
    voice_localEcho = _Dvar_RegisterBool("voice_localEcho", 0, 1u, "Echo voice chat back to the player");
    voice_deadChat = _Dvar_RegisterBool("voice_deadChat", 0, 1u, "Allow dead players to talk to living players");
    g_allowVote = _Dvar_RegisterBool("g_allowVote", 1, 0, "Enable voting on this server");
    g_allow_teamchange = _Dvar_RegisterBool("g_allow_teamchange", 1, 0, "Enable changing teams on this server");
    g_listEntity = _Dvar_RegisterBool("g_listEntity", 0, 0, "List the entities");
    g_listEntityCounts = _Dvar_RegisterEnum(
                                                 "g_listEntityCounts",
                                                 g_entcountNames,
                                                 0,
                                                 0x80u,
                                                 "list all of the current entities counts");
    g_entsInSnapshot = _Dvar_RegisterInt(
                                             "g_entsInSnapshot",
                                             0,
                                             0,
                                             0x7FFFFFFF,
                                             0x80u,
                                             "the number of ents in the snapshot (read only)");
    g_maxEntsInSnapshot = _Dvar_RegisterInt(
                                                    "g_maxEntsInSnapshot",
                                                    0,
                                                    0,
                                                    0x7FFFFFFF,
                                                    0x80u,
                                                    "the high water mark number of ents in all snapshots (read only)");
    g_deadChat = _Dvar_RegisterBool("g_deadChat", 0, 1u, "Allow dead players to chat with living players");
    g_voiceChatTalkingDuration = _Dvar_RegisterInt(
                                                                 "g_voiceChatTalkingDuration",
                                                                 500,
                                                                 0,
                                                                 10000,
                                                                 1u,
                                                                 "Time after the last talk packet was received that the player is considered by the\n"
                                                                 "server to still be talking in milliseconds");
    g_TeamIcon_Allies = _Dvar_RegisterString(
                                                "g_TeamIcon_Allies",
                                                "faction_128_usmc",
                                                0x100u,
                                                "Shader name for the allied scores banner");
    g_TeamIcon_Axis = _Dvar_RegisterString(
                                            "g_TeamIcon_Axis",
                                            "faction_128_arab",
                                            0x100u,
                                            "Shader name for the axis scores banner");
    g_TeamIcon_Free = _Dvar_RegisterString(
                                            "g_TeamIcon_Free",
                                            (char *)"",
                                            0x100u,
                                            "Shader name for the scores of players with no team");
    g_TeamIcon_Spectator = _Dvar_RegisterString(
                                                     "g_TeamIcon_Spectator",
                                                     (char *)"",
                                                     0x100u,
                                                     "Shader name for the scores of players who are spectators");
    g_ScoresColor_MyTeam = _Dvar_RegisterColor(
                                                     "g_ScoresColor_MyTeam",
                                                     0.25,
                                                     0.72000003,
                                                     0.25,
                                                     1.0,
                                                     0x100u,
                                                     "Player team color on scoreboard");
    g_ScoresColor_EnemyTeam = _Dvar_RegisterColor(
                                                            "g_ScoresColor_EnemyTeam",
                                                            0.69,
                                                            0.07,
                                                            0.050000001,
                                                            1.0,
                                                            0x100u,
                                                            "Enemy team color on scoreboard");
    g_ScoresColor_Spectator = _Dvar_RegisterColor(
                                                            "g_ScoresColor_Spectator",
                                                            0.25,
                                                            0.25,
                                                            0.25,
                                                            1.0,
                                                            0x100u,
                                                            "Spectator team color on scoreboard");
    g_ScoresColor_Free = _Dvar_RegisterColor(
                                                 "g_ScoresColor_Free",
                                                 0.75999999,
                                                 0.77999997,
                                                 0.1,
                                                 1.0,
                                                 0x100u,
                                                 "Free Team color on scoreboard");
    g_ScoresColor_Allies = _Dvar_RegisterColor(
                                                     "g_ScoresColor_Allies",
                                                     0.090000004,
                                                     0.46000001,
                                                     0.07,
                                                     1.0,
                                                     0x100u,
                                                     "Allies team color on scoreboard");
    g_ScoresColor_Axis = _Dvar_RegisterColor(
                                                 "g_ScoresColor_Axis",
                                                 0.69,
                                                 0.07,
                                                 0.050000001,
                                                 1.0,
                                                 0x100u,
                                                 "Axis team color on scoreboard");
    g_ScoresPing_Interval = _Dvar_RegisterInt(
                                                        "cg_ScoresPing_Interval",
                                                        1,
                                                        1,
                                                        1,
                                                        0x40u,
                                                        "Number of milliseconds each bar represents");
    g_TeamName_Allies = _Dvar_RegisterString("g_TeamName_Allies", "GAME_ALLIES", 0x100u, "Allied team name");
    g_TeamName_Axis = _Dvar_RegisterString("g_TeamName_Axis", "GAME_AXIS", 0x100u, "Axis team name");
    g_TeamColor_Allies = _Dvar_RegisterColor(
                                                 "g_TeamColor_Allies",
                                                 0.60000002,
                                                 0.63999999,
                                                 0.69,
                                                 1.0,
                                                 0x100u,
                                                 "Allies team color");
    g_TeamColor_Axis = _Dvar_RegisterColor(
                                             "g_TeamColor_Axis",
                                             0.64999998,
                                             0.56999999,
                                             0.41,
                                             1.0,
                                             0x100u,
                                             "Axis team color");
    g_TeamColor_MyTeam = _Dvar_RegisterColor(
                                                 "g_TeamColor_MyTeam",
                                                 0.40000001,
                                                 0.69999999,
                                                 0.40000001,
                                                 1.0,
                                                 0x100u,
                                                 "Player team color");
    g_TeamColor_EnemyTeam = _Dvar_RegisterColor(
                                                        "g_TeamColor_EnemyTeam",
                                                        1.0,
                                                        0.315,
                                                        0.34999999,
                                                        1.0,
                                                        0x100u,
                                                        "Enemy team color");
    g_TeamColor_MyTeamAlt = _Dvar_RegisterColor(
                                                        "g_TeamColor_MyTeamAlt",
                                                        0.34999999,
                                                        1.0,
                                                        1.0,
                                                        1.0,
                                                        0x100u,
                                                        "Player team color");
    g_TeamColor_EnemyTeamAlt = _Dvar_RegisterColor(
                                                             "g_TeamColor_EnemyTeamAlt",
                                                             1.0,
                                                             0.5,
                                                             0.0,
                                                             1.0,
                                                             0x100u,
                                                             "Enemy team color");
    g_TeamColor_Squad = _Dvar_RegisterColor("g_TeamColor_Squad", 0.25, 0.25, 0.75, 1.0, 0x100u, "Squad color");
    g_TeamColor_Spectator = _Dvar_RegisterColor(
                                                        "g_TeamColor_Spectator",
                                                        0.25,
                                                        0.25,
                                                        0.25,
                                                        1.0,
                                                        0x100u,
                                                        "Spectator team color");
    g_TeamColor_Free = _Dvar_RegisterColor("g_TeamColor_Free", 0.75, 0.25, 0.25, 1.0, 0x100u, "Free Team color");
    g_debugLocDamage = _Dvar_RegisterInt(
                                             "g_debugLocDamage",
                                             0,
                                             0,
                                             2,
                                             0x80u,
                                             "Turn on debugging information for locational damage (2 = show results of bullet trace pose only)");
    g_debugLocHit = _Dvar_RegisterInt(
                                        "g_debugLocHit",
                                        0,
                                        0,
                                        2,
                                        0x80u,
                                        "Display locational damage info for an entity when the entity is hit");
    g_debugLocHitTime = _Dvar_RegisterInt(
                                                "g_debugLocHitTime",
                                                500,
                                                0,
                                                0x7FFFFFFF,
                                                0x80u,
                                                "Time duration of g_debugLocHit lines");
    g_smoothClients = _Dvar_RegisterBool("g_smoothClients", 1, 0, "Enable extrapolation between client states");
    g_antilag = _Dvar_RegisterBool("g_antilag", 1, 0x40u, "Turn on antilag checks for weapon hits");
    g_oldVoting = _Dvar_RegisterBool("g_oldVoting", 1, 1u, "Use old voting method");
    g_voteAbstainWeight = _Dvar_RegisterFloat(
                                                    "g_voteAbstainWeight",
                                                    0.5,
                                                    0.0,
                                                    1.0,
                                                    1u,
                                                    "How much an abstained vote counts as a 'no' vote");
    g_NoScriptSpam = _Dvar_RegisterBool("g_no_script_spam", 0, 0, "Turn off script debugging info");
    g_friendlyfireDist = _Dvar_RegisterFloat(
                                                 "g_friendlyfireDist",
                                                 BG_ZombieFloatDefault("g_friendlyfireDist", 256.0f, 175.0f),
                                                 0.0,
                                                 15000.0,
                                                 0x80u,
                                                 "Maximum range for disabling fire at a friendly");
    g_friendlyNameDist = _Dvar_RegisterFloat(
                                                 "g_friendlyNameDist",
                                                 15000.0,
                                                 0.0,
                                                 15000.0,
                                                 0x80u,
                                                 "Maximum range for seeing a friendly's name");
    melee_debug = _Dvar_RegisterBool("melee_debug", 0, 0x80u, "Turn on debug lines for melee traces");
    radius_damage_debug = _Dvar_RegisterBool(
                                                    "radius_damage_debug",
                                                    0,
                                                    0x80u,
                                                    "Turn on debug lines for radius damage traces");
    player_throwbackInnerRadius = _Dvar_RegisterFloat(
                                                                    "player_throwbackInnerRadius",
                                                                    BG_ZombieFloatDefault("player_throwbackInnerRadius", 90.0f, 72.0f),
                                                                    0.0,
                                                                    3.4028235e38,
                                                                    0x80u,
                                                                    "The radius to a live grenade player must be within initially to do a throwback");
    player_throwbackOuterRadius = _Dvar_RegisterFloat(
                                                                    "player_throwbackOuterRadius",
                                                                    BG_ZombieFloatDefault("player_throwbackOuterRadius", 160.0f, 192.0f),
                                                                    0.0,
                                                                    3.4028235e38,
                                                                    0x80u,
                                                                    "The radius player is allow to throwback a grenade once the player has been in the inner radius");
    player_useRadius = _Dvar_RegisterFloat(
                                             "player_useRadius",
                                             128.0,
                                             0.0,
                                             3.4028235e38,
                                             0x3080u,
                                             "The radius within which a player can use things");
    player_MGUseRadius = _Dvar_RegisterFloat(
                                                 "player_MGUseRadius",
                                                 128.0,
                                                 0.0,
                                                 3.4028235e38,
                                                 0x80u,
                                                 "The radius within which a player can mount a machine gun");
    vehicle_useRadius = _Dvar_RegisterFloat(
                                                "vehicle_useRadius",
                                                256.0,
                                                0.0,
                                                3.4028235e38,
                                                0x80u,
                                                "The radius within which a player can enter a vehicle");
    g_minGrenadeDamageSpeed = _Dvar_RegisterFloat(
                                                            "g_minGrenadeDamageSpeed",
                                                            400.0,
                                                            0.0,
                                                            3.4028235e38,
                                                            0x80u,
                                                            "Minimum speed at which getting hit be a grenade will do damage (not the grenade explosion damage)");
    g_compassShowEnemies = _Dvar_RegisterBool(
                                                     "g_compassShowEnemies",
                                                     0,
                                                     0x80u,
                                                     "Whether enemies are visible on the compass at all times");
    pickupPrints = _Dvar_RegisterBool(
                                     "pickupPrints",
                                     0,
                                     0x80u,
                                     "Print a message to the game window when picking up ammo, etc.");
    g_revive = _Dvar_RegisterBool("g_revive", 0, 0, "Enable revive");
    g_dumpAnims = _Dvar_RegisterInt(
                                    "g_dumpAnims",
                                    -1,
                                    -1,
                                    1023,
                                    0x80u,
                                    "Animation debugging info for the given character number");
    g_useholdtime = _Dvar_RegisterInt(
                                        "g_useholdtime",
                                        250,
                                        0,
                                        0x7FFFFFFF,
                                        0,
                                        "Time to hold the 'use' button to activate use");
    g_useholdspawndelay = _Dvar_RegisterInt(
                                                    "g_useholdspawndelay",
                                                    500,
                                                    0,
                                                    1000,
                                                    0x81u,
                                                    "Time in milliseconds that the player is unable to 'use' after spawning");
    g_redCrosshairs = _Dvar_RegisterBool("g_redCrosshairs", 1, 0x21u, "Whether red crosshairs are enabled");
    g_mantleBlockTimeBuffer = _Dvar_RegisterInt(
                                                            "g_mantleBlockTimeBuffer",
                                                            500,
                                                            0,
                                                            60000,
                                                            0x80u,
                                                            "Time that the client think is delayed after mantling");
    g_vehicleDebug = _Dvar_RegisterBool("g_vehicleDebug", 0, 0x80u, "Turn on debug information for vehicles");
    vehGunnerSplashDamage = _Dvar_RegisterFloat(
                                                        "vehGunnerSplashDamage",
                                                        0.1,
                                                        0.0,
                                                        3.4028235e38,
                                                        0x80u,
                                                        "Percentage of projectile and grenade splash damage that vehicle gunners take.");
    turretPlayerAvoidScale = _Dvar_RegisterFloat(
                                                         "turretPlayerAvoidScale",
                                                         1.7,
                                                         0.0,
                                                         3.4028235e38,
                                                         0x80u,
                                                         "Auto turrets will try to avoid the player.    They will not choose a target that is within a "
                                                         "cone around the player.    The diameter of the cone is the player's height, so the cone is sm"
                                                         "aller, the farther the player is from the turret.    Use this dvar to scale the cone size.");
    g_enableAttachWeaponFix = _Dvar_RegisterBool(
                                                            "g_enableAttachWeaponFix",
                                                            1,
                                                            0,
                                                            "Enable fix for attach weapons being out of sync on server/client");
    anim_deltas_debug = _Dvar_RegisterBool("anim_deltas_debug", 0, 0, "Enable animation debug data");
    g_destructibleDraw = _Dvar_RegisterBool("g_destructibleDraw", 0, 0x80u, "Render destructible debug info");
    g_debugServerAiming = _Dvar_RegisterBool(
                                                    "g_debugServerAiming",
                                                    0,
                                                    0x80u,
                                                    "Render where the server thinks the client is aiming");
    Helicopter_RegisterDvars();
    Turret_RegisterDvars();
    G_RegisterMissileDvars();
    G_RegisterMissileDebugDvars();
    Turret_PlaceTurret_RegisterDvars();
    DynEntSv_RegisterDvars();
    G_RegisterConnectPaths();
    BG_RegisterDvars();
    g_fogColorReadOnly = _Dvar_RegisterColor(
                                                 "g_fogColorReadOnly",
                                                 1.0,
                                                 0.0,
                                                 0.0,
                                                 1.0,
                                                 0x10C0u,
                                                 "Fog color that was set in the most recent call to \"setexpfog\"");
    g_fogStartDistReadOnly = _Dvar_RegisterFloat(
                                                         "g_fogStartDistReadOnly",
                                                         0.0,
                                                         0.0,
                                                         3.4028235e38,
                                                         0x10C0u,
                                                         "Fog start distance that was set in the most recent call to \"setexpfog\"");
    g_fogHalfDistReadOnly = _Dvar_RegisterFloat(
                                                        "g_fogHalfDistReadOnly",
                                                        0.1,
                                                        0.0,
                                                        3.4028235e38,
                                                        0x10C0u,
                                                        "Fog start distance that was set in the most recent call to \"setexpfog\"");
    vehPlaneRollDeadZone = _Dvar_RegisterFloat(
                                                     "vehPlaneRollDeadZone",
                                                     0.15000001,
                                                     0.0,
                                                     1.0,
                                                     0x80u,
                                                     "Roll input dead zone. Percentage of stick movement to ignore for roll.");
    vehPlaneRollAccel = _Dvar_RegisterFloat(
                                                "vehPlaneRollAccel",
                                                16.0,
                                                0.0,
                                                3.4028235e38,
                                                0x80u,
                                                "Adjustable rotation scaler.");
    vehPlanePitchAccel = _Dvar_RegisterFloat(
                                                 "vehPlanePitchAccel",
                                                 16.0,
                                                 0.0,
                                                 3.4028235e38,
                                                 0x80u,
                                                 "Adjustable rotation scaler.");
    vehPlaneYawSpeed = _Dvar_RegisterFloat(
                                             "vehPlaneYawSpeed",
                                             50.0,
                                             0.0,
                                             3.4028235e38,
                                             0x80u,
                                             "Adjustable rotation scaler.");
    vehPlaneYawFromRollScale = _Dvar_RegisterFloat(
                                                             "vehPlaneYawFromRollScale",
                                                             0.1,
                                                             0.0,
                                                             3.4028235e38,
                                                             0x80u,
                                                             "Adjustable rotation scaler.");
    vehPlaneLiftForce = _Dvar_RegisterFloat("vehPlaneLiftForce", 0.0, 0.0, 3.4028235e38, 0x80u, "Lift force on aircraft.");
    vehPlaneFakeLiftForce = _Dvar_RegisterFloat(
                                                        "vehPlaneFakeLiftForce",
                                                        0.0,
                                                        0.0,
                                                        3.4028235e38,
                                                        0x80u,
                                                        "Fake lift force on aircraft.    Adds extra pitch when non-horizontal.");
    vehPlaneLowSpeed = _Dvar_RegisterFloat("vehPlaneLowSpeed", 1500.0, 0.0, 3.4028235e38, 0x80u, "Min speed.");
    vehPlaneGravityForce = _Dvar_RegisterFloat(
                                                     "vehPlaneGravityForce",
                                                     0.0,
                                                     0.0,
                                                     3.4028235e38,
                                                     0x80u,
                                                     "Gravity force on aircraft.");
    vehicle_switch_seat_delay = _Dvar_RegisterFloat(
                                                                "vehicle_switch_seat_delay",
                                                                0.5,
                                                                0.0,
                                                                3.4028235e38,
                                                                0x80u,
                                                                "Delay before player can switch seats again.");
    vehicle_damage_max_shielding = _Dvar_RegisterFloat(
                                                                     "vehicle_damage_max_shielding",
                                                                     0.25,
                                                                     0.0,
                                                                     1.0,
                                                                     0x80u,
                                                                     "Percent of core damage that armor can shield.");
    vehicle_damage_zone_front = _Dvar_RegisterFloat(
                                                                "vehicle_damage_zone_front",
                                                                1.0,
                                                                0.0,
                                                                3.4028235e38,
                                                                0x80u,
                                                                "Front zone damage for vehicles.");
    vehicle_damage_zone_side = _Dvar_RegisterFloat(
                                                             "vehicle_damage_zone_side",
                                                             1.2,
                                                             0.0,
                                                             3.4028235e38,
                                                             0x80u,
                                                             "Side zone damage for vehicles.");
    vehicle_damage_zone_rear = _Dvar_RegisterFloat(
                                                             "vehicle_damage_zone_rear",
                                                             2.0,
                                                             0.0,
                                                             3.4028235e38,
                                                             0x80u,
                                                             "Rear zone damage for vehicles.");
    vehicle_damage_zone_under = _Dvar_RegisterFloat(
                                                                "vehicle_damage_zone_under",
                                                                2.25,
                                                                0.0,
                                                                3.4028235e38,
                                                                0x80u,
                                                                "Bottom zone damage for vehicles.");
    vehicle_damage_bullet = _Dvar_RegisterFloat(
                                                        "vehicle_damage_bullet",
                                                        0.0,
                                                        0.0,
                                                        3.4028235e38,
                                                        0x80u,
                                                        "Bullet damage for vehicles.");
    vehicle_damage_grenade = _Dvar_RegisterFloat(
                                                         "vehicle_damage_grenade",
                                                         0.30000001,
                                                         0.0,
                                                         3.4028235e38,
                                                         0x80u,
                                                         "Grenade damage for vehicles.");
    vehicle_damage_projectile = _Dvar_RegisterFloat(
                                                                "vehicle_damage_projectile",
                                                                1.2,
                                                                0.0,
                                                                3.4028235e38,
                                                                0x80u,
                                                                "Projectile damage for vehicles.");
    vehicle_damage_bouncing_betty = _Dvar_RegisterFloat(
                                                                        "vehicle_damage_bouncing_betty",
                                                                        1.25,
                                                                        0.0,
                                                                        3.4028235e38,
                                                                        0x80u,
                                                                        "Bouncing betty damage for vehicles.");
    vehicle_damage_satchel_charge = _Dvar_RegisterFloat(
                                                                        "vehicle_damage_satchel_charge",
                                                                        1.5,
                                                                        0.0,
                                                                        3.4028235e38,
                                                                        0x80u,
                                                                        "Satchel charge damage for vehicles.");
    vehicle_damage_sticky_grenade = _Dvar_RegisterFloat(
                                                                        "vehicle_damage_sticky_grenade",
                                                                        1.35,
                                                                        0.0,
                                                                        3.4028235e38,
                                                                        0x80u,
                                                                        "Sticky grenade damage for vehicles.");
    vehicle_piece_damagesfx_threshold = _Dvar_RegisterFloat(
                                                                                "vehicle_piece_damagesfx_threshold",
                                                                                10.0,
                                                                                0.0,
                                                                                3.4028235e38,
                                                                                0x80u,
                                                                                "Minimum amount of damage for which a destructible piece damageSound SFX will be played.");
    vehicle_destructible_damage_grenade = _Dvar_RegisterFloat(
                                                                                    "vehicle_destructible_damage_grenade",
                                                                                    0.2,
                                                                                    0.0,
                                                                                    3.4028235e38,
                                                                                    0x80u,
                                                                                    "Grenade damage for destructible armor on vehicles.");
    vehicle_destructible_damage_bouncing_betty = _Dvar_RegisterFloat(
                                                                                                 "vehicle_destructible_damage_bouncing_betty",
                                                                                                 1.0,
                                                                                                 0.0,
                                                                                                 3.4028235e38,
                                                                                                 0x80u,
                                                                                                 "Bouncing betty damage for destructible armor on vehicles.");
    vehicle_destructible_damage_satchel_charge = _Dvar_RegisterFloat(
                                                                                                 "vehicle_destructible_damage_satchel_charge",
                                                                                                 1.5,
                                                                                                 0.0,
                                                                                                 3.4028235e38,
                                                                                                 0x80u,
                                                                                                 "Satchel charge damage for destructible armor on vehicles.");
    vehicle_destructible_damage_sticky_grenade = _Dvar_RegisterFloat(
                                                                                                 "vehicle_destructible_damage_sticky_grenade",
                                                                                                 1.5,
                                                                                                 0.0,
                                                                                                 3.4028235e38,
                                                                                                 0x80u,
                                                                                                 "Sticky grenade damage for destructible armor on vehicles.");
    vehicle_destructible_damage_grenade_radius = _Dvar_RegisterFloat(
                                                                                                 "vehicle_destructible_damage_grenade_radius",
                                                                                                 100.0,
                                                                                                 0.0,
                                                                                                 3.4028235e38,
                                                                                                 0x80u,
                                                                                                 "Radius for grenade damage for destructible armor on vehicles.");
    vehicle_destructible_damage_bouncing_betty_radius = _Dvar_RegisterFloat(
                                                                                                                "vehicle_destructible_damage_bouncing_betty_radius",
                                                                                                                13.1,
                                                                                                                0.0,
                                                                                                                3.4028235e38,
                                                                                                                0x80u,
                                                                                                                "Radius for bouncing betty damage for destructible armor on vehicles.");
    vehicle_destructible_damage_satchel_charge_radius = _Dvar_RegisterFloat(
                                                                                                                "vehicle_destructible_damage_satchel_charge_radius",
                                                                                                                21.1,
                                                                                                                0.0,
                                                                                                                3.4028235e38,
                                                                                                                0x80u,
                                                                                                                "Radius for satchel charge damage for destructible armor on vehicles.");
    vehicle_destructible_damage_sticky_grenade_radius = _Dvar_RegisterFloat(
                                                                                                                "vehicle_destructible_damage_sticky_grenade_radius",
                                                                                                                13.1,
                                                                                                                0.0,
                                                                                                                3.4028235e38,
                                                                                                                0x80u,
                                                                                                                "Radius for sticky grenade damage for destructible armor on vehicles.");
    vehicle_destructible_damage_projectile_radius = _Dvar_RegisterFloat(
                                                                                                        "vehicle_destructible_damage_projectile_radius",
                                                                                                        4.0,
                                                                                                        0.0,
                                                                                                        3.4028235e38,
                                                                                                        0x80u,
                                                                                                        "Radius for projectile damage for destructible armor on vehicles.");
    vehicle_perk_leadfoot_speed_increase = _Dvar_RegisterFloat(
                                                                                     "vehicle_perk_leadfoot_speed_increase",
                                                                                     1.35,
                                                                                     0.0,
                                                                                     3.4028235e38,
                                                                                     0,
                                                                                     "Vehicle perk leadfoot speed increase percentage.");
    g_turretServerPitchMin = _Dvar_RegisterFloat(
                                                         "g_turretServerPitchMin",
                                                         15.0,
                                                         -50.0,
                                                         50.0,
                                                         0x80u,
                                                         "Limit turret pitch range on server (visual only)");
    g_turretServerPitchMax = _Dvar_RegisterFloat(
                                                         "g_turretServerPitchMax",
                                                         10.0,
                                                         -50.0,
                                                         50.0,
                                                         0x80u,
                                                         "Limit turret pitch range on server (visual only)");
    g_turretBipodOffset = _Dvar_RegisterFloat(
        "g_turretBipodOffset",
        17.0,
        -50.0,
        50.0,
        0x80u,
        "Offset bipod mount position on gun by this distance");
}

//void(__cdecl *CreateDObj)(struct DObjModel_s *, unsigned __int16, XAnimTree_s *, int, int, clientInfo_t *);
void __cdecl G_CreateDObj(
                DObjModel_s *dobjModels,
                unsigned __int16 numModels,
                XAnimTree_s *tree,
                int handle,
                int unusedLocalClientNum,
                clientInfo_t *__formal)
{
    if ( unusedLocalClientNum != -1
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1600,
                    0,
                    "unusedLocalClientNum == INVALID_CLIENT_NUMBER\n\t%i, %i",
                    unusedLocalClientNum,
                    -1) )
    {
        __debugbreak();
    }
    Com_ServerDObjCreate(dobjModels, numModels, tree, handle);
}

DObj *__cdecl G_GetDObj(unsigned int handle, int unusedLocalClientNum)
{
    if ( unusedLocalClientNum != -1
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1607,
                    0,
                    "unusedLocalClientNum == INVALID_CLIENT_NUMBER\n\t%i, %i",
                    unusedLocalClientNum,
                    -1) )
    {
        __debugbreak();
    }
    return Com_GetServerDObj(handle);
}

void __cdecl G_SafeDObjFree(unsigned int handle, int unusedLocalClientNum)
{
    if ( unusedLocalClientNum != -1
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    1614,
                    0,
                    "unusedLocalClientNum == INVALID_CLIENT_NUMBER\n\t%i, %i",
                    unusedLocalClientNum,
                    -1) )
    {
        __debugbreak();
    }
    Com_SafeServerDObjFree(handle);
}

void G_LoadAnimTreeInstances()
{
    XAnim_s *anims; // [esp+8h] [ebp-4h]

    for ( int i = 0; i < com_maxclients->current.integer; ++i )
        level_bgs.clientinfo[i].pXAnimTree = XAnimCreateTree(level_bgs.animData->generic_human.tree.anims, Hunk_AllocXAnimServer);
    for ( int i = 0; i < 4; ++i )
        g_scr_data.playerCorpseInfo[i].tree = XAnimCreateTree(level_bgs.animData->generic_human.tree.anims, Hunk_AllocXAnimServer);

    // zombies: SP's actor corpses use the actors' generic_human animtree (SP 0x007E2CA0); MP's are dogs
    anims = G_SP_IsSPLevel() ? G_SP_GetActorAnims() : Dog_GetAnims();
    iassert(anims);

    // zombies: SP G_LoadAnimTreeInstances (0x007E2CA0) first fills the 32-slot actor tree bank (0x01C7AE80); an actor
    // uses bank[actor index] (0x00566030) and G_CorpseFromActor (0x00642B80) swaps it with its corpse slot's tree.
    // The one tree SP creates between the bank and the corpses (0x01C87500) has no reader on this path: not ported.
    if ( G_SP_IsSPLevel() )
    {
        for ( int i = 0; i < MAX_ACTORS; ++i ) // mod: was ARRAY_COUNT(actorXAnimTrees) = 32
            G_ActorXAnimTreeSlot(i) = XAnimCreateTree(anims, (void* (*)(unsigned int))Hunk_AllocActorXAnimServer);
    }

    // zombies: 32 corpse slots on an SP level (Com_GetMaxActorCorpses), MP 8
    for ( int i = 0; i < Com_GetMaxActorCorpses(); ++i )
    {
        g_scr_data.actorCorpseInfo[i].tree = XAnimCreateTree(anims, (void* (*)(unsigned int))Hunk_AllocActorXAnimServer);
        g_scr_data.actorCorpseInfo[i].entnum = -1;
    }
}

void *__cdecl Hunk_AllocActorXAnimServer(int size)
{
    return Hunk_AllocLow(size, "Hunk_AllocActorXAnimServer", 5);
}

void G_PrintAllFastFileErrors()
{
    if ( !sv_mapname
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 1852, 0, "%s", "sv_mapname") )
    {
        __debugbreak();
    }
    // zombies: SP G_InitGame (0x0051E8F0) has no MP-zone error-report pass.
    // Keep the build-error checks, using the SP zones selected by the zombie loader.
    G_PrintFastFileErrors(G_SP_IsSPLevel() ? (char*)"code_post_gfx" : (char*)"code_post_gfx_mp");
    // zombies: the SP front end loads no common zone (SP 0x004C8940..0x004C8966): nothing to report there.
    // Nor does zombietron (Com_LoadCommonFastFile, SP 0x0082CB50).
    if ( !Com_IsSPFrontEndRunning() && !(zombietron && zombietron->current.enabled) )
        G_PrintFastFileErrors(G_SP_IsSPLevel() ? (char*)"common_zombie" : (char*)"common_mp");
    G_PrintFastFileErrors((char *)sv_mapname->current.integer);
}

void __cdecl G_PrintFastFileErrors(char *fastfile)
{
    RawFile *rawfile; // [esp+4h] [ebp-4h]

    if ( !fastfile
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 1836, 0, "%s", "fastfile") )
    {
        __debugbreak();
    }
    rawfile = DB_FindXAssetHeader(ASSET_TYPE_RAWFILE, fastfile, 1, -1).rawfile;
    if ( !rawfile
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 1839, 1, "%s", "rawfile") )
    {
        __debugbreak();
    }
    if ( rawfile->len )
    {
        Com_PrintError(1, "There were errors when building fast file '%s'\n", fastfile);
        Com_PrintError(1, (char *)rawfile->buffer);
    }
}

unsigned __int16 __cdecl G_AttachWeapon(DObjModel_s *dobjModels, unsigned __int16 numModels, clientInfo_t *ci)
{
    int oldLeftHand; // [esp+4h] [ebp-Ch]
    unsigned __int8 weaponModel; // [esp+Bh] [ebp-5h]
    const WeaponDef *weapDef; // [esp+Ch] [ebp-4h]
    const WeaponDef *weapDefa; // [esp+Ch] [ebp-4h]

    if ( ci->iDObjWeapon )
    {
        weapDef = BG_GetWeaponDef(ci->iDObjWeapon);
        weaponModel = ci->weaponModel;
        if ( g_enableAttachWeaponFix->current.enabled )
        {
            if ( weapDef->worldModel[weaponModel] && !weapDef->bHideThirdPerson )
            {
                if ( numModels >= 0x20u
                    && !Assert_MyHandler(
                                "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                                1898,
                                0,
                                "%s",
                                "numModels < DOBJ_MAX_SUBMODELS") )
                {
                    __debugbreak();
                }
                dobjModels[numModels].model = weapDef->worldModel[weaponModel];
                dobjModels[numModels].boneName = G_GetWeaponAttachBone(ci, weapDef->weapType, weapDef->inventoryType);
                dobjModels[numModels++].ignoreCollision = 1;
            }
        }
        else if ( weapDef->worldModel[weaponModel] )
        {
            if ( numModels >= 0x20u
                && !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                            1911,
                            0,
                            "%s",
                            "numModels < DOBJ_MAX_SUBMODELS") )
            {
                __debugbreak();
            }
            dobjModels[numModels].model = weapDef->worldModel[weaponModel];
            dobjModels[numModels].boneName = G_GetWeaponAttachBone(ci, weapDef->weapType, weapDef->inventoryType);
            dobjModels[numModels++].ignoreCollision = 1;
        }
        if ( weapDef->bDualWield && !ci->usingKnife )
        {
            weapDef = BG_GetWeaponDef(weapDef->dualWieldWeaponIndex);
            if ( weapDef->worldModel[weaponModel] )
            {
                if ( numModels >= 0x20u
                    && !Assert_MyHandler(
                                "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                                1929,
                                0,
                                "%s",
                                "numModels < DOBJ_MAX_SUBMODELS") )
                {
                    __debugbreak();
                }
                dobjModels[numModels].model = weapDef->worldModel[weaponModel];
                oldLeftHand = ci->leftHandGun;
                ci->leftHandGun = 1;
                ci->leftHandGun = oldLeftHand;
                dobjModels[numModels].boneName = G_GetWeaponAttachBone(ci, weapDef->weapType, weapDef->inventoryType);
                dobjModels[numModels++].ignoreCollision = 1;
            }
        }
        if ( weapDef->additionalMeleeModel )
        {
            if ( numModels >= 0x20u
                && !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                            1944,
                            0,
                            "%s",
                            "numModels < DOBJ_MAX_SUBMODELS") )
            {
                __debugbreak();
            }
            dobjModels[numModels].model = weapDef->additionalMeleeModel;
            dobjModels[numModels].boneName = scr_const.tag_weapon_left;
            dobjModels[numModels++].ignoreCollision = 0;
        }
        else if ( ci->usingKnife )
        {
            if ( ci->iDObjMeleeWeapon )
            {
                weapDefa = BG_GetWeaponDef(ci->iDObjMeleeWeapon);
                if ( weapDefa->worldModel )
                {
                    if ( numModels >= 0x20u
                        && !Assert_MyHandler(
                                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                                    1957,
                                    0,
                                    "%s",
                                    "numModels < DOBJ_MAX_SUBMODELS") )
                    {
                        __debugbreak();
                    }
                    dobjModels[numModels].model = weapDefa->worldModel[ci->meleeWeaponModel];
                    dobjModels[numModels].boneName = scr_const.tag_weapon_left;
                    dobjModels[numModels++].ignoreCollision = 0;
                }
            }
        }
    }
    return numModels;
}

void __cdecl G_ShutdownGame(int freeScripts)
{
    colgeom_visitor_inlined_t<200> *p_proximity_data; // [esp+0h] [ebp-14h]
    int i; // [esp+Ch] [ebp-8h]
    int file; // [esp+10h] [ebp-4h]

    for ( i = 0; i < 32; ++i )
    {
        p_proximity_data = &g_pmove[i].proximity_data;
        p_proximity_data->reset();
        //colgeom_visitor_inlined_t<500>::reset(p_proximity_data);
    }
    ShutdownRopes();
    GlassSv_Shutdown();
    Com_Printf(15, "==== ShutdownGame (%d) ====\n", freeScripts);
    if ( level.logFile )
    {
        G_LogPrintf("ShutdownGame:\n");
        G_LogPrintf("------------------------------------------------------------\n");
        FS_FCloseFile(level.logFile);
    }
    //*(unsigned int *)(*((unsigned int *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 8) = 0;
    bgs = 0;
    G_FreeEntities(1);
    HudElem_DestroyAll();
    Path_Shutdown();
    G_FreeScrVehicles();
    G_FreeVehiclePaths();
    // zombies: SP releases threat group names before shutting down the VM (SP 0x00607700).
    if (G_SP_IsSPLevel())
    {
        Actor_FreeThreatBiasGroups();
        // zombies: SP G_ShutdownGame clears the setplayergravity flag of the 4 clients (SP 0x00607945).
        if (zombiemode && zombiemode->current.enabled)
            for (SPPlayerGravity &gravity : g_spPlayerGravity)
                gravity.gravityOverride = false;
        // zombies: SP G_ShutdownGame releases the gettagorigin/gettagangles cached tag names
        // (Scr_SetString 0x00406BD0 on 0x01C05354 and 0x01C05390, SP 0x00607862 and 0x0060786E). KB only released
        // cachedTagMat at quit (GScr_Shutdown), so G_InitGame's memset(&level) dropped the ref on
        // every map change (game over -> restart) and SL_CheckLeaks asserted at quit.
        Scr_SetString(&level.cachedTagMat.name, 0, SCRIPTINSTANCE_SERVER);
        Scr_SetString(&level.cachedEntTargetTagMat.name, 0, SCRIPTINSTANCE_SERVER);
    }
    if ( Scr_IsSystemActive(1u, SCRIPTINSTANCE_SERVER) && !level.savepersist )
        SV_FreeClientScriptPers();
    Scr_ShutdownSystem(SCRIPTINSTANCE_SERVER, 1u, level.savepersist == 0);
    if ( freeScripts )
    {
        Mantle_ShutdownAnims();
        Dog_ShutdownAnims();
        GScr_FreeScripts(SCRIPTINSTANCE_SERVER);
        Scr_FreeScripts(SCRIPTINSTANCE_SERVER, 1u);
        G_FreeAnimTreeInstances();
        Scr_ShutdownAnimTrees(1);
        Com_FreeWeaponInfoMemory(1);
        Hunk_ClearToMarkLow(0);
    }
    for ( file = 0; file < 1; ++file )
    {
        if ( level.openScriptIOFileBuffers[file] )
            Z_VirtualFree(level.openScriptIOFileBuffers[file], 11);
        level.openScriptIOFileBuffers[file] = 0;
        if ( level.openScriptIOFileHandles[file] )
            FS_FCloseFile(level.openScriptIOFileHandles[file]);
        level.openScriptIOFileHandles[file] = 0;
    }
    EntHandle::Shutdown();
    SV_track_shutdown();
}

void __cdecl CalculateRanks()
{
    unsigned int clientNum; // [esp+0h] [ebp-8h]
    int i; // [esp+4h] [ebp-4h]
    int ia; // [esp+4h] [ebp-4h]

    level.numConnectedClients = 0;
    level.numVotingClients = 0;
    for ( i = 0; i < level.maxclients; ++i )
    {
        if ( level.clients[i].sess.connected )
        {
            level.sortedClients[level.numConnectedClients++] = i;
            if ( level.clients[i].sess.cs.team != TEAM_SPECTATOR && level.clients[i].sess.connected == CON_CONNECTED )
                ++level.numVotingClients;
        }
    }
    qsort(level.sortedClients, level.numConnectedClients, 4u, (int (__cdecl *)(const void *, const void *))SortRanks);
    for ( ia = 0; ia < level.numConnectedClients; ++ia )
    {
        clientNum = level.sortedClients[ia];
        if ( clientNum >= level.maxclients
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                        2613,
                        0,
                        "clientNum doesn't index level.maxclients\n\t%i not in [0, %i)",
                        clientNum,
                        level.maxclients) )
        {
            __debugbreak();
        }
        level.clients[clientNum].sess.cs.score.place = ia + 1;
    }
    level.bUpdateScoresForIntermission = 1;
}

int __cdecl SortRanks(int *a, int *b)
{
    gclient_s *cb; // [esp+0h] [ebp-8h]
    gclient_s *ca; // [esp+4h] [ebp-4h]

    if ( (unsigned int)*a >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    2534,
                    0,
                    "*(int *)a doesn't index level.maxclients\n\t%i not in [0, %i)",
                    *a,
                    level.maxclients) )
    {
        __debugbreak();
    }
    if ( (unsigned int)*b >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    2535,
                    0,
                    "*(int *)b doesn't index level.maxclients\n\t%i not in [0, %i)",
                    *b,
                    level.maxclients) )
    {
        __debugbreak();
    }
    ca = &level.clients[*a];
    cb = &level.clients[*b];
    if ( ca->sess.connected == CON_CONNECTING )
        return 1;
    if ( cb->sess.connected == CON_CONNECTING )
        return -1;
    if ( ca->sess.cs.team == TEAM_SPECTATOR && cb->sess.cs.team == TEAM_SPECTATOR )
    {
        if ( ca >= cb )
            return ca > cb;
        else
            return -1;
    }
    else if ( ca->sess.cs.team == TEAM_SPECTATOR )
    {
        return 1;
    }
    else if ( cb->sess.cs.team == TEAM_SPECTATOR )
    {
        return -1;
    }
    else if ( ca->sess.cs.score.score <= cb->sess.cs.score.score )
    {
        if ( ca->sess.cs.score.score >= cb->sess.cs.score.score )
        {
            if ( ca->sess.cs.score.deaths >= cb->sess.cs.score.deaths )
                return ca->sess.cs.score.deaths > cb->sess.cs.score.deaths;
            else
                return -1;
        }
        else
        {
            return 1;
        }
    }
    else
    {
        return -1;
    }
}

void __cdecl ExitLevel()
{
    int i; // [esp+0h] [ebp-4h]

    Cbuf_AddText(0, "map_rotate\n");
    for ( i = 0; i < level.maxclients; ++i )
    {
        if ( level.clients[i].sess.connected == CON_CONNECTED )
            level.clients[i].sess.connected = CON_CONNECTING;
    }
    G_LogPrintf("ExitLevel: executed\n");
}

void G_LogPrintf(const char *fmt, ...)
{
    char string[1024]; // [esp+10h] [ebp-818h] BYREF
    char *argptr; // [esp+410h] [ebp-418h]
    int tens; // [esp+414h] [ebp-414h]
    char string2[1024]; // [esp+418h] [ebp-410h] BYREF
    int min; // [esp+81Ch] [ebp-Ch]
    int sec; // [esp+820h] [ebp-8h]
    int time1970; // [esp+824h] [ebp-4h]
    va_list va; // [esp+834h] [ebp+Ch] BYREF

    va_start(va, fmt);
    _vsnprintf(string2, sizeof(string2), fmt, va);
    string2[sizeof(string2) - 1] = 0;
    va_end(va);
    G_Mapkit_RecordCheck(string2);
    if ( level.logFile )
    {
        argptr = 0;
        if ( g_logTimeStampInSeconds && g_logTimeStampInSeconds->current.enabled )
        {
            time1970 = Com_RealTime(0, 1);
            Com_sprintf(string, 0x400u, "%d %s", time1970, string2);
        }
        else
        {
            min = level.time / 1000 / 60;
            tens = level.time / 1000 % 60 / 10;
            sec = level.time / 1000 % 60 % 10;
            Com_sprintf(string, 0x400u, "%3i:%i%i %s", min, tens, sec, string2);
        }
        FS_Write(string, &string[strlen(string) + 1] - &string[1], level.logFile);
    }
}

void __cdecl CheckVote()
{
    const char *v0; // eax
    const char *v1; // eax
    const char *v2; // eax
    int passCount; // [esp+20h] [ebp-4h]

    if ( level.voteExecuteTime )
    {
        if ( level.voteExecuteTime < level.time )
        {
            level.voteExecuteTime = 0;
            v0 = va("%s\n", level.voteString);
            Cbuf_AddText(0, v0);
        }
    }
    if ( level.voteTime )
    {
        if ( level.time - level.voteTime >= 0 )
        {
            if ( level.voteYes <= (int)((float)((float)(level.numVotingClients - (level.voteNo + level.voteYes))
                                                                                * g_voteAbstainWeight->current.value)
                                                                + 0.4999999990686774)
                                                    + level.voteNo )
                goto LABEL_11;
LABEL_9:
            v1 = va("%c \"GAME_VOTEPASSED\"", 101);
            SV_GameSendServerCommand(-1, SV_CMD_CAN_IGNORE, v1);
            level.voteExecuteTime = level.time + 3000;
LABEL_13:
            level.voteTime = 0;
            SV_SetConfigstring(15, (char *)"");
            return;
        }
        passCount = level.numVotingClients / 2 + 1;
        if ( level.voteYes >= passCount )
            goto LABEL_9;
        if ( level.voteNo > level.numVotingClients - passCount )
        {
LABEL_11:
            v2 = va("%c \"GAME_VOTEFAILED\"", 101);
            SV_GameSendServerCommand(-1, SV_CMD_CAN_IGNORE, v2);
            goto LABEL_13;
        }
    }
}

void __cdecl G_UpdateObjectiveToClients()
{
    objective_t *obj; // [esp+8h] [ebp-18h]
    int team; // [esp+Ch] [ebp-14h]
    gentity_s *ent; // [esp+10h] [ebp-10h]
    int clientNum; // [esp+14h] [ebp-Ch]
    int objNum; // [esp+18h] [ebp-8h]
    playerState_s *ps; // [esp+1Ch] [ebp-4h]

    for ( clientNum = 0; clientNum < level.maxclients; ++clientNum )
    {
        ent = &level.gentities[clientNum];
        if ( ent->r.inuse )
        {
            if ( !ent->client
                && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 2810, 0, "%s", "ent->client") )
            {
                __debugbreak();
            }
            ps = &ent->client->ps;
            team = ps[1].corpseIndex;
            for ( objNum = 0; objNum < 32; ++objNum )
            {
                obj = &level.objectives[objNum];
                if ( obj->state
                    && (!obj->teamNum || obj->teamNum == team)
                    && (level.objectivesClientMask[objNum][clientNum >> 5] & (1 << (clientNum & 0x1F))) == 0 )
                {
                    memcpy(&ps->objective[objNum], obj, sizeof(ps->objective[objNum]));
                }
                else
                {
                    ps->objective[objNum].state = OBJST_EMPTY;
                }
            }
        }
    }
}

void __cdecl G_UpdateHudElemsToClients()
{
    gentity_s *ent; // [esp+0h] [ebp-8h]
    int clientNum; // [esp+4h] [ebp-4h]

    for ( clientNum = 0; clientNum < level.maxclients; ++clientNum )
    {
        ent = &level.gentities[clientNum];
        if ( ent->r.inuse )
        {
            if ( !ent->client
                && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 2841, 0, "%s", "ent->client") )
            {
                __debugbreak();
            }
            HudElem_UpdateClient(ent->client, ent->s.number, HUDELEM_UPDATE_ARCHIVAL_AND_CURRENT);
        }
    }
}

void __cdecl G_RunThink(gentity_s *ent)
{
    void (__cdecl *think)(gentity_s *); // [esp+0h] [ebp-8h]
    int thinktime; // [esp+4h] [ebp-4h]

    thinktime = ent->nextthink;
    if ( thinktime > 0 && thinktime <= level.time )
    {
        ent->nextthink = 0;
        think = entityHandlers[ent->handler].think;
        if ( !think )
            Com_Error(ERR_DROP, "NULL ent->think");
        think(ent);
    }
}

void __cdecl DebugDumpAnims()
{
    if ( g_dumpAnims->current.integer >= 0 )
    {
        if ( g_dumpAnims->current.integer >= 1024
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                        2885,
                        1,
                        "%s",
                        "g_dumpAnims->current.integer < MAX_GENTITIES") )
        {
            __debugbreak();
        }
        SV_DObjDisplayAnim(&level.gentities[g_dumpAnims->current.integer], "server:\n");
    }
}

void __cdecl G_XAnimUpdateEnt(gentity_s *ent)
{
    if ( ent->r.inuse )
    {
        if ( (ent->flags & 0x2000) == 0 )
            G_DObjUpdateServerTime(ent, 1, ScriptPump);
    }
}

void __cdecl ScriptPump()
{
    Scr_RunCurrentThreads(SCRIPTINSTANCE_SERVER);
}

// zombies: SP g_entityNeedsRerun (0x01c07080), set by the anim pass's notetrack callback.
static bool g_entityNeedsRerun;

// zombies: SP anim-pass notetrack callback (LAB_007e3180): drain the woken script threads, then ask the anim
// pass to rerun the entities whose anims a script changed (G_FlagAnimForUpdate).
static void __cdecl ScriptPump_SP()
{
    Scr_RunCurrentThreads(SCRIPTINSTANCE_SERVER);
    g_entityNeedsRerun = true;
}

int lastEntTime;
void __cdecl ShowEntityInfo()
{
    int v0; // [esp+0h] [ebp-A4h]
    int integer; // [esp+4h] [ebp-A0h]
    float vEnd[3]; // [esp+Ch] [ebp-98h] BYREF
    float vForward[3]; // [esp+54h] [ebp-50h] BYREF
    unsigned __int16 hitEntId; // [esp+60h] [ebp-44h]
    void (__cdecl *entinfo)(gentity_s *, float *); // [esp+64h] [ebp-40h]
    col_context_t context; // [esp+68h] [ebp-3Ch] BYREF
    gentity_s *ent; // [esp+90h] [ebp-14h]
    int i; // [esp+94h] [ebp-10h]
    float vStart[3]; // [esp+98h] [ebp-Ch] BYREF

    //col_context_t::col_context_t(&context);
    CL_GetDebugViewPos(vStart);
    if ( g_radiant_selected_ent )
        misc_EntInfo(g_radiant_selected_ent, vStart);
    if ( g_entinfo->current.integer )
    {
        if ( g_entinfo->current.integer == 2
            || g_entinfo->current.integer == 3
            || g_entinfo->current.integer == 5
            || g_entinfo->current.integer == 7 )
        {
            trace_t trace; // [esp+18h] [ebp-8Ch] BYREF
            CL_GetDebugViewForward(vForward);
            vEnd[0] = (float)(16000.0 * vForward[0]) + vStart[0];
            vEnd[1] = (float)(16000.0 * vForward[1]) + vStart[1];
            vEnd[2] = (float)(16000.0 * vForward[2]) + vStart[2];
            integer = g_entinfo_type->current.integer;
            if ( integer == 1 )
            {
                G_TraceCapsule(&trace, vStart, vec3_origin, vec3_origin, vEnd, 0, 0x8000, &context);
            }
            else if ( integer == 2 )
            {
                G_TraceCapsule(&trace, vStart, vec3_origin, vec3_origin, vEnd, 0, 0x800000, &context);
            }
            else
            {
                G_TraceCapsule(&trace, vStart, vec3_origin, vec3_origin, vEnd, 0, 0x80A080, &context);
            }
            hitEntId = Trace_GetEntityHitId(&trace);
            entinfo = entityHandlers[g_entities[hitEntId].handler].entinfo;
            if ( entinfo )
            {
                Dvar_SetIntByName("ai_debugEntIndex", hitEntId);
                lastEntTime = level.time;
            }
            if ( ai_debugEntIndex->current.integer != -1 && lastEntTime + 30000 > level.time )
            {
                ent = &g_entities[ai_debugEntIndex->current.integer];
                if ( ent->actor && ai_debugCoverEntityNum->current.integer > 0 )
                    Dvar_SetInt((dvar_s *)ai_debugCoverEntityNum, ent->s.number);
                entinfo = entityHandlers[ent->handler].entinfo;
                if ( entinfo )
                    entinfo(ent, vStart);
            }
            return;
        }
        ent = g_entities;
        i = 0;
        while ( 1 )
        {
            if ( i >= level.num_entities )
                return;
            if ( ent->r.inuse && ent->r.linked )
            {
                if ( g_entinfo->current.integer == 6 )
                {
                    if ( (ent->r.svFlags & 1) != 0 || ent->classname != scr_const.script_model && !ent->client )
                        goto LABEL_24;
                }
                else
                {
                    v0 = g_entinfo_type->current.integer;
                    if ( v0 == 1 )
                    {
                        if ( !ent->actor )
                            goto LABEL_24;
                    }
                    else if ( v0 == 2 && !ent->scr_vehicle )
                    {
                        goto LABEL_24;
                    }
                }
                entinfo = entityHandlers[ent->handler].entinfo;
                if ( entinfo )
                    entinfo(ent, vStart);
            }
LABEL_24:
            ++i;
            ++ent;
        }
    }
}

void __cdecl G_UpdateIKPlayerClipTerrainTimeout(gentity_s *ent)
{
    if ( ent->client )
    {
        if ( (ent->client->ps.eFlags2 & 0x10000) != 0 && ent->ikPlayerclipTerrainTime < level.time - 1000 )
            ent->client->ps.eFlags2 &= ~0x10000u;
    }
    else if ( (ent->s.lerp.eFlags2 & 0x10000) != 0 && ent->ikPlayerclipTerrainTime < level.time - 1000 )
    {
        ent->s.lerp.eFlags2 &= ~0x10000u;
    }
}

void __cdecl G_UpdateIKDisableTerrainMappingTimeout(gentity_s *ent)
{
    if ( ent->client )
    {
        if ( (ent->client->ps.eFlags2 & 0x100000) != 0 && ent->ikDisableTerrainMappingTime < level.time - 1000 )
            ent->client->ps.eFlags2 &= ~0x100000u;
    }
    else if ( (ent->s.lerp.eFlags2 & 0x100000) != 0 && ent->ikDisableTerrainMappingTime < level.time - 1000 )
    {
        ent->s.lerp.eFlags2 &= ~0x100000u;
    }
}

static float dir[3];
static float hitPos[3];
void __cdecl G_UpdateTimedDamage(gentity_s *ent)
{
    int max; // [esp+Ch] [ebp-10h]
    float dist; // [esp+10h] [ebp-Ch]
    int i; // [esp+14h] [ebp-8h]
    gentity_s *fireStarterEnt; // [esp+18h] [ebp-4h]

    if ( level.time >= ent->last_timed_radius_damage )
    {
        max = 0;
        for ( i = 0; i < g_timed_radius_damage_count; ++i )
        {
            if ( level.time < g_timed_radius_damage[i].life )
            {
                max = i + 1;
                dist = Vec3DistanceSq(g_timed_radius_damage[i].pos, ent->r.currentOrigin);
                if ( g_timed_radius_damage[i].radiusSqr >= dist )
                {
                    ent->last_timed_radius_damage = g_timed_radius_damage[i].rate + level.time;
                    if ( g_timed_radius_damage[i].fireStarterClientNum < 0x20u )
                    {
                        fireStarterEnt = &g_entities[g_timed_radius_damage[i].fireStarterClientNum];
                        G_Damage(
                            ent,
                            fireStarterEnt,
                            fireStarterEnt,
                            dir,
                            hitPos,
                            g_timed_radius_damage[i].damage,
                            1,
                            g_timed_radius_damage[i].mod,
                            g_timed_radius_damage[i].weapon,
                            HITLOC_NONE,
                            0,
                            0,
                            0);
                    }
                }
            }
        }
        if ( max < g_timed_radius_damage_count )
            g_timed_radius_damage_count = max;
    }
}

void __cdecl G_DebugTimedDamage()
{
    int i; // [esp+14h] [ebp-4h]

    if ( fire_debug->current.enabled )
    {
        for ( i = 0; i < g_timed_radius_damage_count; ++i )
        {
            if ( level.time < g_timed_radius_damage[i].life )
                G_DebugCircle(g_timed_radius_damage[i].pos, g_timed_radius_damage[i].radius, colorRed, 1, 0, 0);
        }
    }
}

void __cdecl G_UpdateClientLinkInfo(gentity_s *ent)
{
    char *v1; // eax
    float *v2; // [esp+4h] [ebp-14h]
    int tagIndex; // [esp+10h] [ebp-8h]
    gentity_s *entParent; // [esp+14h] [ebp-4h]

    if ( !ent && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 3486, 0, "%s", "ent") )
        __debugbreak();
    if ( ent->tagChildren )
        ent->s.clientLinkInfo.flags |= 1u;
    else
        ent->s.clientLinkInfo.flags &= ~1u;
    if ( ent )
    {
        if ( ent->tagInfo )
        {
            if ( !ent->tagInfo->parent
                && !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                            3503,
                            0,
                            "%s",
                            "ent->tagInfo->parent") )
            {
                __debugbreak();
            }
            entParent = ent->tagInfo->parent;
            if ( (entParent->r.svFlags & 1) != 0 )
            {
                if ( (float)((float)((float)(entParent->s.lerp.pos.trDelta[0] * entParent->s.lerp.pos.trDelta[0])
                                                     + (float)(entParent->s.lerp.pos.trDelta[1] * entParent->s.lerp.pos.trDelta[1]))
                                     + (float)(entParent->s.lerp.pos.trDelta[2] * entParent->s.lerp.pos.trDelta[2])) != 0.0
                    || (float)((float)((float)(entParent->s.lerp.apos.trDelta[0] * entParent->s.lerp.apos.trDelta[0])
                                                     + (float)(entParent->s.lerp.apos.trDelta[1] * entParent->s.lerp.apos.trDelta[1]))
                                     + (float)(entParent->s.lerp.apos.trDelta[2] * entParent->s.lerp.apos.trDelta[2])) != 0.0 )
                {
                    Com_PrintWarning(
                        15,
                        "Ent #%i of type %i is SVF_NOCLIENT, but has children and seems to be moving.    There is a potential optimization here.\n",
                        ent->s.number,
                        ent->s.eType);
                }
                ent->s.clientLinkInfo.parentEnt = 0;
            }
            else if ( entParent->s.number != 1022 && EntNum_IsNetworked(entParent->s.number) ) // mod (L25): server-only entity numbers 1024+ are not sent
            {
                if ( ent->s.clientLinkInfo.parentEnt != ent->tagInfo->parent->s.number + 1 )
                {
                    AssignToSmallerType<short>((__int16 *)&ent->s.clientLinkInfo, ent->tagInfo->parent->s.number + 1);
                    if ( ent->tagInfo->name )
                    {
                        v1 = SL_ConvertToString(ent->tagInfo->name, SCRIPTINSTANCE_SERVER);
                        tagIndex = G_TagIndex(v1);
                        AssignToSmallerType<unsigned char>(&ent->s.clientLinkInfo.tagIndex, tagIndex);
                        ent->s.clientLinkInfo.flags &= ~2u;
                    }
                    else
                    {
                        ent->s.clientLinkInfo.tagIndex = 0;
                        ent->s.clientLinkInfo.flags |= 2u;
                    }
                }
                v2 = ent->tagInfo->axis[3];
                ent->s.lerp.pos.trDelta[0] = *v2;
                ent->s.lerp.pos.trDelta[1] = v2[1];
                ent->s.lerp.pos.trDelta[2] = v2[2];
                AxisToAngles(ent->tagInfo->axis, ent->s.lerp.apos.trDelta);
                ResolveParentClientMask(ent, entParent);
            }
        }
        else
        {
            ent->s.clientLinkInfo.parentEnt = 0;
            ent->s.clientLinkInfo.tagIndex = 0;
        }
    }
}

bool __cdecl ResolveParentClientMask(const gentity_s *entChild, gentity_s *entParent)
{
    int checkBits; // [esp+0h] [ebp-Ch]
    int seg; // [esp+4h] [ebp-8h]
    bool changed; // [esp+Bh] [ebp-1h]

    if ( !entChild
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 3455, 0, "%s", "entChild") )
    {
        __debugbreak();
    }
    if ( !entParent
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 3456, 0, "%s", "entParent") )
    {
        __debugbreak();
    }
    changed = 0;
    for ( seg = 0; seg < 1; ++seg )
    {
        checkBits = entParent->r.clientMask[seg] & ~entChild->r.clientMask[seg];
        if ( checkBits )
        {
            changed = 1;
            entParent->r.clientMask[seg] &= ~checkBits;
        }
    }
    return changed;
}

void    G_RunFrame(int levelTime)
{
    int Time; // eax
    int v3; // eax
    trigger_info_t *v4; // eax
    char *v5; // eax
    gentity_s *other; // [esp+54h] [ebp-424h]
    trigger_info_t *trigger_info; // [esp+58h] [ebp-420h]
    unsigned __int8 index; // [esp+5Fh] [ebp-419h]
    unsigned __int8 entIndex[MAX_GENTITIES_SV + 4]; // mod (L25): server-only entity numbers 1024+ // [esp+60h] [ebp-418h] BYREF
    gentity_s *ent; // [esp+468h] [ebp-10h]
    int bMoreTriggered; // [esp+46Ch] [ebp-Ch]
    int i; // [esp+470h] [ebp-8h]
    int entnum; // [esp+474h] [ebp-4h]

    gScrExecuteTime[0] = 0;
    SV_CheckThread();
    ++level.framenum;
    level.previousTime = level.time;
    level.time = levelTime;
    level.frametime = levelTime - level.previousTime;
    SND_BO1TraceSetTime(false, levelTime); // zombies (a1): bo1_audio line clock, trace only
    level_bgs.time = levelTime;
    G_BO1EntCensus(false); // mod (L43): bo1_entcensus
    // zombies: advance the BGS clock for every server frame (SP 0x0045A6E8).
    // Brief said collection owns this clock; exe writes a DIFFERENT global at
    // 0x006048E6 (0x028890E0), not level_bgs + 8 (0x01BF1410).
    level_bgs.latestSnapshotTime = levelTime;
    level_bgs.frametime = levelTime - level.previousTime;
    iassert(bgs == 0);
    bgs = &level_bgs;
    if ( level.frametime < 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    3584,
                    0,
                    "%s",
                    "level.frametime >= 0") )
    {
        __debugbreak();
    }

    G_SP_ProfileBegin(); // k1: headless slow-frame profile (measurement only)
    {
        PROF_SCOPED("Update clients");
        SVPROF_SCOPED(SVPROF_CLIENT); // mod (L43): bo1_mod_svprof
        ent = g_entities;
        i = 0;
        while (i < level.maxclients)
        {
            if (ent->r.inuse)
            {
                if (!ent->client
                    && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 3600, 0, "%s", "ent->client"))
                {
                    __debugbreak();
                }
                if ((ent->client->flags & 3) == 0)
                    G_DoTouchTriggers(ent);
                G_UpdateWeapons(ent);
                G_UpdateTimedDamage(ent);
                G_UpdateIKPlayerClipTerrainTimeout(ent);
                G_UpdateIKDisableTerrainMappingTimeout(ent);
                G_UpdateIKCulling(ent);
            }
            ++i;
            ++ent;
        }
    }

    G_SP_ProfileMark(SP_PROFILE_CLIENTS);
#ifdef BO1_LIVE
    if ( onlinegame->current.enabled && com_sv_running->current.enabled )
        MatchRecordMovement();
#endif
    G_DebugTimedDamage();
    Time = G_GetTime();
    SV_Flame_Age_All_Objects(Time);
    v3 = G_GetTime();
    IK_UpdateTimeAll(v3, -1, 0);

    {
        PROF_SCOPED("G_XAnimUpdate");
        SVPROF_SCOPED(SVPROF_ANIM); // mod (L43): bo1_mod_svprof
        ent = g_entities;
        i = 0;
        while (i < G_EntEnd())
        {
            if (ent->r.inuse)
                SV_DObjInitServerTime(ent, (float)level.frametime * 0.001);
            i = G_EntNext(i);
            ent = &g_entities[i];
        }
    }

    G_SP_ProfileMark(SP_PROFILE_PRE);
    memset(entIndex, 0, MAX_GENTITIES_SV); // mod (L25): server-only entity numbers 1024+
    index = 0;

    {
        PROF_SCOPED("G_TriggerChecks");

        if (level.currentTriggerListSize
            && !Assert_MyHandler(
                "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                3654,
                0,
                "%s",
                "level.currentTriggerListSize == 0"))
        {
            __debugbreak();
        }
        Com_Memcpy(level.currentTriggerList, level.pendingTriggerList, 12 * level.pendingTriggerListSize);
        level.currentTriggerListSize = level.pendingTriggerListSize;
        level.pendingTriggerListSize = 0;
        do
        {
            bMoreTriggered = 0;
            ++index;
            for (i = 0; i < level.currentTriggerListSize; ++i)
            {
                trigger_info = &level.currentTriggerList[i];
                entnum = trigger_info->entnum;
                ent = &g_entities[entnum];
                if (ent->useCount == trigger_info->useCount)
                {
                    iassert(ent->r.inuse);
                    other = &g_entities[trigger_info->otherEntnum];
                    if (other->useCount == trigger_info->otherUseCount)
                    {
                        iassert(other->r.inuse);
                        
                        if (entIndex[entnum] == index)
                        {
                            bMoreTriggered = 1;
                            continue;
                        }
                        entIndex[entnum] = index;
                        Scr_AddEntity(other, SCRIPTINSTANCE_SERVER);
                        Scr_Notify(ent, scr_const.trigger, 1u);
                    }
                }
                --level.currentTriggerListSize;
                --i;
                v4 = &level.currentTriggerList[level.currentTriggerListSize];
                *(unsigned int *)&trigger_info->entnum = *(unsigned int *)&v4->entnum;
                trigger_info->useCount = v4->useCount;
                trigger_info->otherUseCount = v4->otherUseCount;
            }

            {
                PROF_SCOPED("Scr_RunCurrentThreads");
                SVPROF_SCOPED(SVPROF_SCRIPT); // mod (L43): bo1_mod_svprof
                Scr_RunCurrentThreads(SCRIPTINSTANCE_SERVER);
            }
        } while (bMoreTriggered);
        iassert(level.currentTriggerListSize == 0);
    }
    G_SP_ProfileMark(SP_PROFILE_TRIGGERS);
    
    ent = g_entities;
    i = 0;
    while ( i < level.maxclients )
    {
        if ( ent->r.inuse )
            G_ClientDoPerFrameNotifies(ent);
        ++i;
        ++ent;
    }
    G_SP_ProfileMark(SP_PROFILE_NOTIFIES);

    {
        PROF_SCOPED("G_XAnimUpdate");
        SVPROF_SCOPED(SVPROF_ANIM); // mod (L43): bo1_mod_svprof
        if ( zombiemode && zombiemode->current.enabled )
        {
            // zombies: SP G_RunFrame anim pass (SP 0x0045a940..0x0045a9fc). The notetrack callback sets
            // g_entityNeedsRerun; while it is set, every entity a script re-animated (flags 0x40000, set by
            // G_FlagAnimForUpdate, cleared by G_DObjUpdateServerTime) is updated again. The rerun starts each
            // anim from its oldTime, so an anim (re)started by the callback advances this frame and the
            // actor's delta after Scr_IncTime already carries it (a traverse's run anim after the "end"
            // notetrack moves on that frame, as in the recording). SP flag 0x1000 is KB's 0x2000.
            g_entityNeedsRerun = false;
            for ( i = 0, ent = g_entities; i < G_EntEnd(); i = G_EntNext(i), ent = &g_entities[i] )
            {
                if ( ent->r.inuse && (ent->flags & 0x2000) == 0 )
                    G_DObjUpdateServerTime(ent, 1, ScriptPump_SP);
            }
            while ( g_entityNeedsRerun )
            {
                g_entityNeedsRerun = false;
                for ( i = 0, ent = g_entities; i < G_EntEnd(); i = G_EntNext(i), ent = &g_entities[i] )
                {
                    if ( ent->r.inuse && (ent->flags & 0x40000) != 0 && (ent->flags & 0x2000) == 0 )
                        G_DObjUpdateServerTime(ent, 1, ScriptPump_SP);
                }
            }
        }
        else
        {
            ent = g_entities;
            i = 0;
            while (i < G_EntEnd())
            {
                G_XAnimUpdateEnt(ent);
                i = G_EntNext(i);
                ent = &g_entities[i];
            }
        }
    }

    G_SP_ProfileMark(SP_PROFILE_XANIM);
    { SVPROF_SCOPED(SVPROF_SCRIPT); Scr_IncTime(SCRIPTINSTANCE_SERVER); } // mod (L43): bo1_mod_svprof
    SV_ResetSkeletonCache();
    G_SP_ProfileMark(SP_PROFILE_SCRIPT_TIME);
    if (g_maxActors > MAX_ACTORS_RETAIL) // mod (L25): entity / actor use every 5 s of level time, for the limit runs
    {
        static int nextModCountTime;
        if (levelTime < nextModCountTime - 5000)
            nextModCountTime = 0;
        if (levelTime >= nextModCountTime)
        {
            int inuse = 0, actors = 0;
            for (int e = 0; e < G_EntEnd(); e = G_EntNext(e)) // mod (L25): networked entity numbers 1537+
                inuse += g_entities[e].r.inuse ? 1 : 0;
            for (int a = 0; level.actors && a < g_maxActors; ++a)
                actors += level.actors[a].inuse ? 1 : 0;
            Com_Printf(15, "mod: level %d ents inuse %d num_entities %d high %d actors %d/%d scrobj %d/%d scrval %d\n", levelTime, inuse,
                level.num_entities, g_numEntitiesHi, actors, g_maxActors, gScrVarPub[SCRIPTINSTANCE_SERVER].numScriptObjects,
                VARIABLELIST_PARENT_SIZE(SCRIPTINSTANCE_SERVER), gScrVarPub[SCRIPTINSTANCE_SERVER].numScriptValues);
            // census of the low (networked) range by classname, every 30 s: what fills the 1024 wire numbers
            static int modCensusCount;
            if ((modCensusCount++ % 6) == 0)
            {
                unsigned int names[64]; int counts[64]; int n = 0;
                for (int e = 0; e < level.num_entities; ++e)
                {
                    if (!g_entities[e].r.inuse)
                        continue;
                    unsigned int cn = g_entities[e].classname;
                    int k = 0;
                    while (k < n && names[k] != cn)
                        ++k;
                    if (k == n && n < 64)
                    {
                        names[n] = cn; counts[n] = 0; ++n;
                    }
                    if (k < n)
                        ++counts[k];
                }
                char line[1024]; int len = 0;
                line[0] = 0;
                for (int k = 0; k < n && len < 900; ++k)
                    if (counts[k] >= 4)
                        len += Com_sprintf(line + len, sizeof(line) - len, " %s:%d",
                            names[k] ? SL_ConvertToString(names[k], SCRIPTINSTANCE_SERVER) : "?", counts[k]);
                Com_Printf(15, "mod: census%s\n", line);
            }
            nextModCountTime = levelTime + 5000;
        }
    }

    {
        PROF_SCOPED("G_RunFrameForEntity");
        SVPROF_SCOPED(SVPROF_ENTS); // mod (L43): bo1_mod_svprof
        if (level.currentEntityThink != -1
            && !Assert_MyHandler(
                "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                3730,
                0,
                "%s",
                "level.currentEntityThink == -1"))
        {
            __debugbreak();
        }
        ent = g_entities;
        level.currentEntityThink = 0;
        while (level.currentEntityThink < G_EntEnd())
        {
            if (ent->r.inuse)
            {
                const long long thinkStart = G_SP_ProfileNow();
                G_RunFrameForEntity(ent);
                G_UpdateClientLinkInfo(ent);
                G_SP_ProfileEntity(ent, thinkStart);
            }
            level.currentEntityThink = G_EntNext(level.currentEntityThink);
            ent = &g_entities[level.currentEntityThink];
        }
        level.currentEntityThink = -1;
    }
    G_SP_ProfileMark(SP_PROFILE_ENTITIES);
    
    {
        PROF_SCOPED("G_UpdateObjectiveToClients");
        G_UpdateObjectiveToClients();
    }
    {
        PROF_SCOPED("G_UpdateHudElemsToClients");
        G_UpdateHudElemsToClients();
    }
    
    {
        PROF_SCOPED("ClientEndFrame");
        SVPROF_SCOPED(SVPROF_CLIENT); // mod (L43): bo1_mod_svprof

        ent = g_entities;
        i = 0;
        while (i < level.maxclients)
        {
            if (ent->r.inuse)
            {
                if ((unsigned int)i >= 0x20
                    && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                        3765,
                        0,
                        "i doesn't index MAX_CLIENTS\n\t%i not in [0, %i)",
                        i,
                        32))
                {
                    __debugbreak();
                }
                if (!level_bgs.clientinfo[i].infoValid
                    && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                        3766,
                        0,
                        "%s",
                        "level_bgs.clientinfo[i].infoValid"))
                {
                    __debugbreak();
                }
                if (ent->client - level.clients != i
                    && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                        3767,
                        0,
                        "ent->client - level.clients == i\n\t%i, %i",
                        ent->client - level.clients,
                        i))
                {
                    __debugbreak();
                }
                ClientEndFrame(ent);
            }
            ++i;
            ++ent;
        }
    }
    G_SP_ProfileMark(SP_PROFILE_CLIENT_END);
    
    {
        PROF_SCOPED("CheckTeamStatus");
        CheckTeamStatus();
    }
    
    if ( g_oldVoting->current.enabled )
        CheckVote();
    if ( g_listEntity->current.enabled )
    {
        for ( i = 0; i < 1024; ++i )
        {
            v5 = SL_ConvertToString(g_entities[i].classname, SCRIPTINSTANCE_SERVER);
            Com_Printf(15, "%4i: %s\n", i, v5);
        }
        Dvar_SetBool((dvar_s *)g_listEntity, 0);
    }
    if ( level.registerWeapons )
        SaveRegisteredWeapons();
    if ( level.bRegisterItems )
        SaveRegisteredItems();
    SpawnSystem_Update();
    G_PopulateMatchState();
    G_ProcessRadiantCmds();
    // zombies: SP G_RunFrame runs the level-exit check here (SP 0x0045AB15).
    if (G_SP_IsSPLevel())
        G_SP_CheckLevelExit();
    G_UpdateActorCorpses();
    Path_Update();
    DebugDumpAnims();

    iassert(bgs == &level_bgs);
    bgs = 0;
    ShowEntityInfo();
    //BLOPS_NULLSUB();
    GlassSv_Update();
    LiveSteam_Server_RunCallbacks();
    G_SP_MeasureFrame();
    G_SP_HeadlessScriptAtFrame(); // L27 TEST SWITCH bo1_testclient_scriptat (headless only, off by default)
    G_SP_PlayTraceServerFrame(); // j1: bo1_playtrace recorder (tool, off by default)
    G_SP_FeelTraceServerFrame(); // p1 TEST SWITCH bo1_feeltrace (headless client only)
    G_SP_ProfileMark(SP_PROFILE_TAIL);
}

void __cdecl G_ClientDoPerFrameNotifies(gentity_s *ent)
{
    char *v1; // eax
    bool v2; // al
    bool IsSprinting; // al
    bool previouslySprinting; // [esp-Ch] [ebp-14h]
    unsigned __int16 sprint_begin; // [esp-8h] [ebp-10h]
    unsigned __int16 sprint_end; // [esp-4h] [ebp-Ch]
    gclient_s *client; // [esp+4h] [ebp-4h]

    if ( !ent && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 2923, 0, "%s", "ent") )
        __debugbreak();
    client = ent->client;
    if ( !client
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 2927, 0, "%s", "client") )
    {
        __debugbreak();
    }
    if ( client->sess.connected == CON_DISCONNECTED
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    2928,
                    0,
                    "%s",
                    "client->sess.connected != CON_DISCONNECTED") )
    {
        __debugbreak();
    }
    if ( G_SP_IsSPLevel() )
    {
        // zombies: SP per-frame notifier (SP 0x007E3370): weapon_change carries (new, previous), and
        // weapon_change_complete (new) fires on the first frame after a raise (weapState 1 / 2) ends.
        if ( client->ps.weapon != client->lastWeapon )
        {
            Scr_AddString(BG_WeaponName(client->lastWeapon), SCRIPTINSTANCE_SERVER);
            Scr_AddString(BG_WeaponName(client->ps.weapon), SCRIPTINSTANCE_SERVER);
            Scr_Notify(ent, scr_const.weapon_change, 2u);
            client->lastWeapon = client->ps.weapon;
        }
        SPPlayerBuiltinState &spState = G_SP_PlayerBuiltinState(ent->s.number);
        const bool raising = client->ps.weaponstate == WEAPON_RAISING || client->ps.weaponstate == WEAPON_RAISING_ALTSWITCH;
        if ( spState.weaponChangeRaising && !raising )
        {
            Scr_AddString(BG_WeaponName(client->ps.weapon), SCRIPTINSTANCE_SERVER);
            Scr_Notify(ent, scr_const.weapon_change_complete, 1u);
            spState.weaponChangeRaising = false;
        }
        else if ( raising )
        {
            spState.weaponChangeRaising = true;
        }
    }
    else if ( client->ps.weapon != LOWORD(client->lastWeapon) )
    {
        v1 = (char *)BG_WeaponName(client->ps.weapon);
        Scr_AddString(v1, SCRIPTINSTANCE_SERVER);
        Scr_Notify(ent, scr_const.weapon_change, 1u);
        client->lastWeapon = client->ps.weapon;
    }
    if ( client->ps.lastDtpEnd && client->ps.lastDtpEnd == client->ps.commandTime )
        Scr_Notify(ent, scr_const.dtp_end, 0);
    if ( (client->ps.weaponstate == 6 || client->ps.weaponstate == 31) && client->ps.pm_type < 9 )
        v2 = DoPerFrameNotify(ent, 1, client->previouslyFiring, scr_const.begin_firing, scr_const.end_firing);
    else
        v2 = DoPerFrameNotify(ent, 0, client->previouslyFiring, scr_const.begin_firing, scr_const.end_firing);
    client->previouslyFiring = v2;
    client->previouslyUsingNightVision = DoPerFrameNotify(
                                                                                 ent,
                                                                                 (client->ps.weapFlags & 0x40) != 0,
                                                                                 client->previouslyUsingNightVision,
                                                                                 scr_const.night_vision_on,
                                                                                 scr_const.night_vision_off);
    sprint_end = scr_const.sprint_end;
    sprint_begin = scr_const.sprint_begin;
    previouslySprinting = client->previouslySprinting;
    IsSprinting = PM_IsSprinting(&client->ps);
    client->previouslySprinting = DoPerFrameNotify(ent, IsSprinting, previouslySprinting, sprint_begin, sprint_end);
}

bool __cdecl DoPerFrameNotify(
                gentity_s *ent,
                bool isCurrently,
                bool wasPreviously,
                unsigned __int16 begin,
                unsigned __int16 end)
{
    if ( isCurrently == wasPreviously )
        return wasPreviously;
    if ( isCurrently )
        Scr_Notify(ent, begin, 0);
    else
        Scr_Notify(ent, end, 0);
    return isCurrently;
}

void __cdecl G_UpdateIKCulling(gentity_s *ent)
{
    if ( ent->r.inuse )
    {
        if ( ent->client )
        {
            ent->client->ps.eFlags |= 0x20000u;
            if ( (0x90000C & ent->client->ps.pm_flags) == 0
                && ((ent->client->ps.pm_flags & 0x8000) == 0
                 || g_pmove[ent->client->ps.clientNum].xyspeed <= player_sprintThreshhold->current.value) )
            {
                ent->client->ps.eFlags &= ~0x20000u;
            }
        }
    }
}

void __cdecl G_RunFrameForEntity(gentity_s *ent)
{
    char *v1; // eax
    const char *v2; // eax
    char *v3; // eax
    const char *v4; // eax
    char *v5; // eax
    const char *v6; // eax

    iassert(ent->r.inuse);

    if ( ent->processedFrame != level.framenum )
    {
        ent->processedFrame = level.framenum;
        if ( ent->tagInfo )
        {
            iassert(ent->tagInfo->parent);
            G_RunFrameForEntity(ent->tagInfo->parent);
        }

        iassert(((ent->r.svFlags & ((1 << 1) | (1 << 2))) != ((1 << 1) | (1 << 2))));
        iassert(ent->r.maxs[0] >= ent->r.mins[0]);
        iassert(ent->r.maxs[1] >= ent->r.mins[1]);
        iassert(ent->r.maxs[2] >= ent->r.mins[2]);

        if ( ent->s.loopSoundId )
        {
            if ( ent->s.loopSoundFade < 0 )
            {
                ent->s.loopSoundFade += LOWORD(level.time) - LOWORD(level.previousTime);
                if ( ent->s.loopSoundFade >= 0 )
                {
                    ent->s.loopSoundId = 0;
                    ent->s.loopSoundFade = 0;
                }
            }
        }
        // zombies: after loop-sound fade, before entity simulation (SP 0x007E36A0).
        if (G_SP_IsSPLevel())
            G_SP_UpdateSoundNotify(ent);

        if ( ent->scr_vehicle )
        {
            iassert((unsigned)(ent->scr_vehicle - level.vehicles) < MAX_VEHICLES);
            iassert(ent->scr_vehicle->entNum != ENTITYNUM_NONE);
        }
        else
        {
            iassert(ent->s.eType != ET_VEHICLE);
        }

        if ( !ent->client
            && ent->s.eType != ET_VEHICLE
            && ent->s.eType != ET_HELICOPTER
            && ent->s.eType != ET_PLAYER_CORPSE
            && (ent->s.lerp.eFlags & 0x4000) != 0
            && level.time > ent->s.time2 )
        {
            Scr_Notify(ent, scr_const.death, 0);
            G_FreeEntity(ent);
            return;
        }
        if ( level.time - ent->eventTime > 300 )
        {
            if ( ent->freeAfterEvent )
            {
                // zombies: SP 0x007E34D0 gives an EV_PHYS_EXPLOSION_SPHERE temp ent's slot back to G_TempEntity's cap
                if ( ent->s.eType == ET_EVENTS + EV_PHYS_EXPLOSION_SPHERE && g_physExplosionSphereCount )
                    --g_physExplosionSphereCount;
                G_FreeEntity(ent);
                return;
            }
            if ( ent->unlinkAfterEvent )
            {
                ent->unlinkAfterEvent = 0;
                SV_UnlinkEntity(ent);
            }
        }
        if ( !ent->freeAfterEvent )
        {
            switch ( ent->s.eType )
            {
                case ET_MISSILE:
                    G_RunMissile(ent);
                    if ( ent->tagInfo )
                        G_GeneralLink(ent);
                    return;
                case ET_ITEM:
                    if ( ent->tagInfo )
                    {
                        G_GeneralLink(ent);
                        G_RunThink(ent);
                        return;
                    }
LABEL_63:
                    G_RunItem(ent);
                    return;
                case ET_PLAYER_CORPSE:
                    G_RunCorpse(ent);
                    return;
                case ET_ACTOR_CORPSE:
                    // zombies: SP G_RunFrameForEntity (0x007E34D0) has no corpse case. Its generic rules run a
                    // ragdolling corpse's think only, a physics object (every corpse, G_CorpseFromActor 0x00642B80)
                    // through G_RunItem (fall, then Actor_OrientCorpseToGround), anything else with flame damage.
                    if ( G_SP_IsSPLevel() )
                    {
                        if ( Com_IsRagdollTrajectory(&ent->s.lerp.pos) )
                        {
                            G_RunThink(ent);
                            return;
                        }
                        if ( ent->physicsObject )
                            goto LABEL_63;
                        SV_Flame_Apply_Damage(ent);
                    }
                    G_RunThink(ent);
                    return;
            }
            if ( ent->physicsObject )
                goto LABEL_63;
            if ( ent->s.eType == ET_SCRIPTMOVER || ent->s.eType == ET_PLANE || ent->s.eType == ET_PRIMARY_LIGHT)
            {
                SV_Flame_Apply_Damage(ent);
                G_RunMover(ent);
            }
            else if ( ent->client )
            {
                G_RunClient(ent);
            }
            else
            {
                if ( !ent->s.eType || ent->s.eType == 20 )
                {
                    if ( ent->tagInfo )
                        G_GeneralLink(ent);
                }
                SV_Flame_Apply_Damage(ent);
                G_RunThink(ent);
            }
        }
    }
}

void    G_UpdateWeapons(gentity_s *ent)
{
    weaponParms wp; // [esp+4h] [ebp-48h] BYREF

    if ( ent->client )
    {
        if ( ent->client->sess.sessionState == SESS_STATE_SPECTATOR )
            return;
        Weapon_SetWeaponParamsWeapon(&wp, ent->client->ps.weapon);
    }
    else
    {
        if ( !ent->scr_vehicle )
            return;
        Weapon_SetWeaponParamsWeapon(&wp, ent->s.weapon);
    }
    if ( wp.weapDef->weapClass == WEAPCLASS_GAS || wp.weapDef->weapType == WEAPTYPE_GAS )
    {
        G_CalcMuzzlePoints(ent, &wp, 0);
        Weapon_Flamethrower_Update(ent, &wp);
    }
    if ( ent->client )
        Weapon_Overheat_Update(ent);
}

int G_PopulateMatchState()
{
    const char *v0; // eax
    int result; // eax

    level.matchState.index = 0;
    level.matchState.unarchivedState.alliesScore = level.teamScores[2];
    level.matchState.unarchivedState.axisScore = level.teamScores[1];
    v0 = va("scr_%s_scorelimit", g_gametype->current.string);
    result = Dvar_GetInt(v0);
    level.matchState.unarchivedState.scoreLimit = result;
    level.matchState.unarchivedState.mapCenter[0] = svs.mapCenter[0];
    level.matchState.unarchivedState.mapCenter[1] = svs.mapCenter[1];
    level.matchState.unarchivedState.mapCenter[2] = svs.mapCenter[2];
    // zombies: SP's score_s carries headshots (+0x30, read by the SP scoreboard column 11 at 0x008920aa; the
    // scripts' `attacker.headshots++` in maps/_gameskill). BO1Zombies's MP clientState has no such field, so in
    // zombiemode the headshots go to the client through the MP scoreboard-column transport (KB transport,
    // not the SP wire format). Scripts that set their own columns (setscoreboardcolumns) are left alone.
    // On zombietron the other columns carry the HUD's boosters, lives and score multiplier (g_scr_sp_zombietron.h).
    if ( zombiemode && zombiemode->current.enabled
        && level.matchState.unarchivedState.scoreboardColumnTypes[0] == SB_TYPE_INVALID )
    {
        const bool zt = zombietron && zombietron->current.enabled;
        level.matchState.unarchivedState.scoreboardColumnTypes[0] = SB_TYPE_HEADSHOTS;
        level.matchState.unarchivedState.scoreboardColumnTypes[1] = zt ? SB_ZT_BOOSTERS : SB_TYPE_NONE;
        level.matchState.unarchivedState.scoreboardColumnTypes[2] = zt ? SB_ZT_LIVES : SB_TYPE_NONE;
        level.matchState.unarchivedState.scoreboardColumnTypes[3] = zt ? SB_ZT_MULTIPLIER : SB_TYPE_NONE;
    }
    return result;
}

bool __cdecl G_IsEntWalkable(int localClientNum, int entityNum)
{
    return g_entities[entityNum].client == 0;
}

bool __cdecl G_GetEntityOriginAngles(int localClientNum, int entityNum, float *origin, float *angles)
{
    gentity_s *ent; // [esp+8h] [ebp-4h]

    ent = &g_entities[entityNum];
    if ( !ent->r.inuse )
        return 0;
    *origin = ent->r.currentOrigin[0];
    origin[1] = ent->r.currentOrigin[1];
    origin[2] = ent->r.currentOrigin[2];
    *angles = ent->r.currentAngles[0];
    angles[1] = ent->r.currentAngles[1];
    angles[2] = ent->r.currentAngles[2];
    return 1;
}

void __cdecl G_EntityLinkFromPMove(unsigned int entityNum, int parentEntityNum, int tagName)
{
    gentity_s *parent; // [esp+0h] [ebp-8h]

    if ( entityNum >= 0x20
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    3852,
                    0,
                    "%s",
                    "entityNum >= 0 && entityNum < MAX_CLIENTS") )
    {
        __debugbreak();
    }
    if ( (parentEntityNum < 32 || parentEntityNum >= 1024)
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp",
                    3853,
                    0,
                    "%s",
                    "parentEntityNum >= MAX_CLIENTS && parentEntityNum < MAX_GENTITIES") )
    {
        __debugbreak();
    }
    parent = &g_entities[parentEntityNum];
    if ( !g_entities[entityNum].r.inuse
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 3858, 0, "%s", "ent->r.inuse") )
    {
        __debugbreak();
    }
    if ( !parent->r.inuse
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game_mp\\g_main_mp.cpp", 3859, 0, "%s", "parent->r.inuse") )
    {
        __debugbreak();
    }
    G_EntLinkToWithOffset(&g_entities[entityNum], parent, tagName, vec3_origin, vec3_origin);
}

void __cdecl G_AddDebugString(const float *xyz, const float *color, float scale, char *pszText, int duration)
{
    CL_AddDebugString(xyz, color, scale, pszText, duration);
}

gclient_s::gclient_s()
{
    int m; // [esp+8h] [ebp-34h]
    int k; // [esp+10h] [ebp-2Ch]
    int j; // [esp+18h] [ebp-24h]
    int i; // [esp+20h] [ebp-1Ch]

    //clientSession_t::clientSession_t(&this->sess);
    for ( i = 0; i < 2; ++i )
        this->button_bits.array[i] = 0;
    for ( j = 0; j < 2; ++j )
        this->oldbutton_bits.array[j] = 0;
    for ( k = 0; k < 2; ++k )
        this->latched_button_bits.array[k] = 0;
    for ( m = 0; m < 2; ++m )
        this->button_bitsSinceLastFrame.array[m] = 0;
    //return this;
}

// mod (L43): bo1_mod_svprof 1 - the capacity benchmark's server line (tools/stress.ps1). Every 5 s of real time:
// BO1_SVPROF with the server frame (SV_PreFrame..SV_PostFrame, QueryPerformanceCounter) mean / p50 / p99 / max, the
// exclusive ms per frame of each SvProfCat (profile.h), calls per frame of path / trace, actors in use, the process's
// used address space and the renderer overflow counters. Measurement only: nothing here changes the game.
extern int g_bo1SnapEntsMax, g_bo1SnapBytesMax; // mod (L55): sv_snapshot_mp.cpp
static const char *const s_svProfNames[SVPROF_COUNT] = { "other", "ents", "actor", "path", "anim", "trace", "script", "builtin", "snap", "client", "teammove", "physics" };
// script sampler: every ~1 ms, while the server thread is in SVPROF_SCRIPT, count the server VM's current frame position
// (function_frame->fs.pos, set at calls and builtin calls) in a small open-addressing table; names resolved at report
#define SVPROF_SAMPLE_SLOTS 4096
static const char *volatile s_svSamplePos[SVPROF_SAMPLE_SLOTS];
static volatile unsigned int s_svSampleCount[SVPROF_SAMPLE_SLOTS];
// bo1_mod_svprof 2: outside SVPROF_SCRIPT, suspend the server thread and count its instruction pointer (per category);
// the report names the functions through dbghelp (BO1_SVPC)
static volatile unsigned int s_svPcAddr[SVPROF_SAMPLE_SLOTS];
static volatile unsigned int s_svPcCount[SVPROF_SAMPLE_SLOTS];
static volatile unsigned char s_svPcCat[SVPROF_SAMPLE_SLOTS];
static void G_ModSvProfSamplePc(int cat)
{
    static HANDLE thread;
    static unsigned long threadId;
    if (threadId != g_svProfThread)
    {
        if (thread)
            CloseHandle(thread);
        threadId = g_svProfThread;
        thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId);
    }
    if (!thread || SuspendThread(thread) == (DWORD)-1)
        return;
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_CONTROL;
    const BOOL ok = GetThreadContext(thread, &ctx);
    const int depth = g_svProfDepth;
    if (depth >= 0 && depth <= 63)
        cat = g_svProfStack[depth];
    ResumeThread(thread);
    if (!ok || !ctx.Eip)
        return;
    const unsigned int pc = ctx.Eip;
    unsigned int h = (pc * 2654435761u) >> 20;
    for (int probe = 0; probe < 16; ++probe, h = (h + 1) & (SVPROF_SAMPLE_SLOTS - 1))
    {
        if (!s_svPcAddr[h])
        {
            s_svPcAddr[h] = pc;
            s_svPcCat[h] = (unsigned char)cat;
        }
        if (s_svPcAddr[h] == pc)
        {
            ++s_svPcCount[h];
            break;
        }
    }
}

static DWORD WINAPI G_ModSvProfSampler(void *)
{
    for (;;)
    {
        Sleep(1);
        if (!g_svProfOn || !g_svProfThread)
            continue;
        const int depth = g_svProfDepth;
        if (depth < 0 || depth > 63)
            continue;
        if (g_svProfStack[depth] != SVPROF_SCRIPT)
        {
            if (g_svProfOn == 2)
                G_ModSvProfSamplePc(g_svProfStack[depth]);
            continue;
        }
        const function_frame_t *frame = gScrVmPub[SCRIPTINSTANCE_SERVER].function_frame;
        if (!frame)
            continue;
        const char *pos = frame->fs.pos;
        if (!pos || pos < gScrVarPub[SCRIPTINSTANCE_SERVER].programBuffer || pos >= gScrVarPub[SCRIPTINSTANCE_SERVER].endScriptBuffer)
            continue;
        unsigned int h = ((unsigned int)(size_t)pos * 2654435761u) >> 20;
        for (int probe = 0; probe < 16; ++probe, h = (h + 1) & (SVPROF_SAMPLE_SLOTS - 1))
        {
            if (!s_svSamplePos[h])
                s_svSamplePos[h] = pos;
            if (s_svSamplePos[h] == pos)
            {
                ++s_svSampleCount[h];
                break;
            }
        }
    }
}

// mod (L55): bo1_mod_svab <mask> - an in-run A/B of optimizations that give the same game, for a machine that is never
// quiet: each block of bo1_mod_svab_frames server frames is randomly (a hash of the block number, so no periodic script
// work lines up with it) run with the masked options OFF (arm 0) or ON (arm 1); BO1_SVAB reports each arm's frame time.
// Bits: 1 bo1_mod_moveaway, 2 bo1_mod_sectorlist walks (the arrays are kept either way; needs sectorlist 1 at map
// start), 4 bo1_mod_ailod (not exact: use blocks of seconds), 8 bo1_mod_teammove, 16 bo1_mod_sentientcopy,
// 32 bo1_mod_entcontents, 64 the sentientcopy per-index bound, 128 bo1_mod_entboxcache, 256 bo1_mod_sentientscan. Needs
// bo1_mod_svprof. Measurement only.
int g_modSvAbArm = -1; // -1 = no A/B this frame
int g_modAiLod; // mod (L55): actor_mp / g_scr_main_mp.cpp, set by bo1_mod_ailod or the A/B
int g_modTeamMove; // mod (L55): actor_team_move.cpp, set by bo1_mod_teammove or the A/B
extern int g_modMoveAway;
extern int g_modSectorListUse; // cm_world.cpp
static void G_ModSvAbFrame()
{
    static const dvar_t *ab, *abFrames, *aiLod, *teamMove;
    static unsigned int frame;
    if (!ab)
    {
        ab = _Dvar_RegisterInt("bo1_mod_svab", 0, 0, 1023, 0, "mod: in-run A/B of server options (bit mask, see g_main_mp.cpp)");
        abFrames = _Dvar_RegisterInt("bo1_mod_svab_frames", 1, 1, 10000, 0, "mod: bo1_mod_svab block length in server frames");
        aiLod = _Dvar_RegisterInt("bo1_mod_ailod", 0, 0, 1, 0, "mod: zombies far from every player update their run animation every 0.2 s (retail ai_runAnimUpdateFrequency max) instead of every frame");
        teamMove = _Dvar_RegisterInt("bo1_mod_teammove", 0, 0, 2, 0, "mod: team-move dodge scan visits the sentients near the actor instead of every slot (1 use, 2 verify)");
    }
    const int mask = ab->current.integer;
    g_modAiLod = zombiemode && zombiemode->current.enabled ? aiLod->current.integer : 0;
    static const dvar_t *svFast;
    if (!svFast)
        svFast = Dvar_FindVar("bo1_mod_svfast");
    g_modTeamMove = teamMove->current.integer ? teamMove->current.integer : (svFast ? svFast->current.integer : 0);
    if (!mask)
    {
        g_modSvAbArm = -1;
        return;
    }
    unsigned int x = frame++ / (unsigned int)abFrames->current.integer;
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    g_modSvAbArm = (int)(x & 1);
    if (mask & 1)
        g_modMoveAway = g_modSvAbArm;
    if (mask & 2)
        g_modSectorListUse = g_modSvAbArm;
    if (mask & 4)
        g_modAiLod = g_modSvAbArm && zombiemode && zombiemode->current.enabled;
    if (mask & 8)
        g_modTeamMove = g_modSvAbArm;
    if (mask & 16)
    {
        extern int g_modSentientCopy; // actor_mp.cpp: spawn knowledge copy, zombies with more than the retail actors only
        g_modSentientCopy = g_modSvAbArm && zombiemode && zombiemode->current.enabled && g_maxActors > MAX_ACTORS_RETAIL;
    }
    if (mask & 256)
    {
        extern int g_modSentientScan; // sentient.cpp
        g_modSentientScan = g_modSvAbArm;
    }
    if (mask & 128)
    {
        extern int g_modEntBoxCache; // cm_world.cpp: bo1_mod_entboxcache
        g_modEntBoxCache = g_modSvAbArm;
    }
    if (mask & 64)
    {
        extern int g_modSentientBound; // actor_mp.cpp: the exact per-index bound of the spawn knowledge copy
        g_modSentientBound = g_modSvAbArm;
    }
    if (mask & 32)
    {
        extern int g_modEntContentsCache; // cm_world.cpp
        g_modEntContentsCache = g_modSvAbArm;
    }
}

static int g_svPcoreFrames[3]; // mod (L64): server frames started on unknown / slower / P-core logical processors
int __cdecl Sys_ModOnPCore();
void G_ModSvProfBegin()
{
    G_ModSvAbFrame();
    extern int g_modEntContentsCache; // mod (L55): bo1_mod_entcontents - resync the contents copy once per server frame
    if (g_modEntContentsCache)
    {
        void CM_ModEntContentsSet(int, int);
        for (int i = 0; i < level.num_entities; ++i)
            CM_ModEntContentsSet(i, g_entities[i].r.contents);
        void CM_ModEntContentsSectorRebuild();
        CM_ModEntContentsSectorRebuild();
    }
    extern int g_modSentientScan; // mod (L55): bo1_mod_sentientscan - resync the team copy once per server frame
    if (g_modSentientScan)
    {
        void Sentient_ModTeamResync();
        Sentient_ModTeamResync();
    }
    if (!g_svProfOn)
        return;
    ++g_svPcoreFrames[Sys_ModOnPCore() + 1]; // mod (L64): which core class each server frame starts on
    static bool samplerStarted;
    if (!samplerStarted)
    {
        samplerStarted = true;
        CreateThread(0, 0, G_ModSvProfSampler, 0, 0, 0);
    }
    g_svProfThread = GetCurrentThreadId();
    g_svProfDepth = 0;
    g_svProfStack[0] = SVPROF_OTHER;
    g_svProfLast = __rdtsc();
}

void G_ModSvProfEnd(long long frameQpcTicks)
{
    static unsigned long long winTicks[SVPROF_COUNT];
    static unsigned long long winCalls[SVPROF_COUNT];
    static float frameMs[4096];
    static float abMs[2][4096]; // mod (L55): bo1_mod_svab arms
    static int abN[2];
    static double abSum[2];
    static unsigned long long abTicks[2][SVPROF_COUNT];
    static int frames;
    static long long winQpc0;
    static unsigned long long winTsc0;
    static double sumMs;
    if (!g_svProfOn)
    {
        winQpc0 = 0;
        return;
    }
    const unsigned long long now = __rdtsc();
    g_svProfTicks[g_svProfStack[g_svProfDepth]] += now - g_svProfLast;
    g_svProfLast = now;
    g_svProfDepth = 0;
    g_svProfThread = 0; // scopes between frames (event loop, packet reads) are not frame time: off until G_ModSvProfBegin
    LARGE_INTEGER freq, qpc;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&qpc);
    if (!winQpc0)
    {
        winQpc0 = qpc.QuadPart;
        winTsc0 = now;
        frames = 0;
        sumMs = 0.0;
        memset(winTicks, 0, sizeof(winTicks));
        memset(winCalls, 0, sizeof(winCalls));
        memset(g_svProfTicks, 0, sizeof(g_svProfTicks));
        memset(g_svProfCalls, 0, sizeof(g_svProfCalls));
        return;
    }
    for (int c = 0; c < SVPROF_COUNT; ++c)
    {
        winTicks[c] += g_svProfTicks[c];
        if (g_modSvAbArm >= 0)
            abTicks[g_modSvAbArm][c] += g_svProfTicks[c];
        winCalls[c] += g_svProfCalls[c];
        g_svProfTicks[c] = 0;
        g_svProfCalls[c] = 0;
    }
    const double ms = (double)frameQpcTicks * 1000.0 / (double)freq.QuadPart;
    if (frames < 4096)
        frameMs[frames] = (float)ms;
    if (g_modSvAbArm >= 0)
    {
        if (abN[g_modSvAbArm] < 4096)
            abMs[g_modSvAbArm][abN[g_modSvAbArm]] = (float)ms;
        ++abN[g_modSvAbArm];
        abSum[g_modSvAbArm] += ms;
    }
    ++frames;
    sumMs += ms;
    const double winMs = (double)(qpc.QuadPart - winQpc0) * 1000.0 / (double)freq.QuadPart;
    if (winMs < 5000.0)
        return;
    const int n = frames < 4096 ? frames : 4096;
    std::sort(frameMs, frameMs + n);
    const double msPerTick = winMs / (double)(now - winTsc0);
    int actors = 0;
    for (int a = 0; level.actors && a < g_maxActors; ++a)
        actors += level.actors[a].inuse ? 1 : 0;
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    char cats[512];
    int len = 0;
    for (int c = 0; c < SVPROF_COUNT; ++c)
        len += _snprintf(cats + len, sizeof(cats) - len, "\"%s\":%.2f,", s_svProfNames[c], (double)winTicks[c] * msPerTick / frames);
    Com_Printf(15, "BO1_SVPROF {\"level_time\":%d,\"wall_ms\":%.0f,\"frames\":%d,\"actors\":%d,\"ms_mean\":%.2f,\"ms_p50\":%.2f,"
        "\"ms_p99\":%.2f,\"ms_max\":%.2f,%s\"path_n\":%.1f,\"trace_n\":%.0f,\"va_mb\":%d,\"rw_consts\":%ld,\"rw_skin\":%ld,\"rw_gfxents\":%ld,\"ge_peak\":%ld,\"ge_over256\":%ld,\"snap_ents\":%d,\"snap_bytes\":%d,\"pcore_pct\":%d}\n",
        level.time, winMs, frames, actors, sumMs / frames, frameMs[n / 2], frameMs[(n * 99) / 100 < n ? (n * 99) / 100 : n - 1],
        frameMs[n - 1], cats, (double)winCalls[SVPROF_PATH] / frames, (double)winCalls[SVPROF_TRACE] / frames,
        (int)((mem.ullTotalVirtual - mem.ullAvailVirtual) >> 20), g_modRendWarn[0], g_modRendWarn[1], g_modRendWarn[2],
        _InterlockedExchange(&g_modRendWarn[3], 0), g_modRendWarn[5], g_bo1SnapEntsMax, g_bo1SnapBytesMax,
        g_svPcoreFrames[1] + g_svPcoreFrames[2] ? 100 * g_svPcoreFrames[2] / (g_svPcoreFrames[1] + g_svPcoreFrames[2]) : -1);
    memset(g_svPcoreFrames, 0, sizeof(g_svPcoreFrames));
    g_bo1SnapEntsMax = 0; // mod (L55): per window: snapshot entities wanted, compressed bytes
    g_bo1SnapBytesMax = 0; // mod (L43): ge_peak = this window's, ge_over256 since start
    if (abN[0] || abN[1]) // mod (L55): bo1_mod_svab - this window's frames per arm (mean / p50 ms, ms per frame by category)
    {
        char arms[2][400];
        for (int arm = 0; arm < 2; ++arm)
        {
            const int an = abN[arm] < 4096 ? abN[arm] : 4096;
            std::sort(abMs[arm], abMs[arm] + an);
            int al = _snprintf(arms[arm], sizeof(arms[arm]), "{\"n\":%d,\"mean\":%.2f,\"p50\":%.2f", abN[arm],
                abN[arm] ? abSum[arm] / abN[arm] : 0.0, an ? abMs[arm][an / 2] : 0.0f);
            for (int c = 0; c < SVPROF_COUNT; ++c)
                al += _snprintf(arms[arm] + al, sizeof(arms[arm]) - al, ",\"%s\":%.2f", s_svProfNames[c],
                    abN[arm] ? (double)abTicks[arm][c] * msPerTick / abN[arm] : 0.0);
            _snprintf(arms[arm] + al, sizeof(arms[arm]) - al, "}");
            abN[arm] = 0;
            abSum[arm] = 0.0;
            memset(abTicks[arm], 0, sizeof(abTicks[arm]));
        }
        extern int g_modAiLodHits, g_modAiLodCalls; // g_scr_sp_ai.cpp: this window's far / all getRunAnimUpdateFrequency calls
        Com_Printf(15, "BO1_SVAB {\"level_time\":%d,\"actors\":%d,\"lod_far\":%d,\"lod_calls\":%d,\"off\":%s,\"on\":%s}\n",
            level.time, actors, g_modAiLodHits, g_modAiLodCalls, arms[0], arms[1]);
        g_modAiLodHits = g_modAiLodCalls = 0;
    }
    // script functions by samples (G_ModSvProfSampler): the 12 functions most often running while the server thread
    // was in SVPROF_SCRIPT; share of the window's script samples in percent
    {
        static char names[256][64];
        static unsigned int counts[256];
        int numNames = 0;
        unsigned int total = 0;
        for (int i = 0; i < SVPROF_SAMPLE_SLOTS; ++i)
        {
            const char *pos = s_svSamplePos[i];
            const unsigned int count = s_svSampleCount[i];
            s_svSampleCount[i] = 0;
            s_svSamplePos[i] = 0;
            if (!pos || !count)
                continue;
            total += count;
            const char *fn = gScrVarPub[SCRIPTINSTANCE_SERVER].developer ? Scr_PrevCodePosFunctionName(SCRIPTINSTANCE_SERVER, (char *)pos) : 0;
            char name[64];
            _snprintf(name, sizeof(name), "%s", fn ? fn : "?");
            name[sizeof(name) - 1] = 0;
            for (char *c = name; *c; ++c)
                if (*c == '"' || *c == '\\' || *c < ' ')
                    *c = '_';
            int k = 0;
            while (k < numNames && strcmp(names[k], name))
                ++k;
            if (k == numNames)
            {
                if (numNames == 256)
                    continue;
                strcpy(names[numNames], name);
                counts[numNames++] = 0;
            }
            counts[k] += count;
        }
        char top[1024];
        int topLen = 0;
        top[0] = 0;
        for (int t = 0; t < 12 && total; ++t)
        {
            int best = -1;
            for (int k = 0; k < numNames; ++k)
                if (counts[k] && (best < 0 || counts[k] > counts[best]))
                    best = k;
            if (best < 0)
                break;
            topLen += _snprintf(top + topLen, sizeof(top) - topLen, "%s\"%s\":%.1f", t ? "," : "", names[best], 100.0 * counts[best] / total);
            if (topLen >= (int)sizeof(top) - 1)
                break;
            counts[best] = 0;
        }
        top[sizeof(top) - 1] = 0;
        Com_Printf(15, "BO1_SVSCRIPT {\"level_time\":%d,\"samples\":%u,\"top\":{%s}}\n", level.time, total, top);
    }
    // the 10 builtins with the most time this window, names from the PDB (dbghelp, loaded on first use)
    {
        typedef BOOL(WINAPI * SymInit_t)(HANDLE, PCSTR, BOOL);
        typedef BOOL(WINAPI * SymFromAddr_t)(HANDLE, DWORD64, PDWORD64, void *);
        static SymFromAddr_t symFromAddr;
        static bool symTried;
        if (!symTried)
        {
            symTried = true;
            HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
            SymInit_t symInit = dbghelp ? (SymInit_t)GetProcAddress(dbghelp, "SymInitialize") : 0;
            if (symInit && symInit(GetCurrentProcess(), 0, TRUE))
                symFromAddr = (SymFromAddr_t)GetProcAddress(dbghelp, "SymFromAddr");
        }
        char top[1024];
        int topLen = 0;
        for (int k = 0; k < 10; ++k)
        {
            int best = -1;
            for (int i = 0; i < SVPROF_MAX_BUILTINS; ++i)
                if (g_svProfBuiltinCalls[i] && (best < 0 || g_svProfBuiltinTicks[i] > g_svProfBuiltinTicks[best]))
                    best = i;
            if (best < 0)
                break;
            const void *fn = gScrCompilePub[SCRIPTINSTANCE_SERVER].func_table ? (const void *)gScrCompilePub[SCRIPTINSTANCE_SERVER].func_table[best] : 0;
            char name[64];
            _snprintf(name, sizeof(name), "b%d", best);
            struct { SYMBOL_INFO info; char more[256]; } sym;
            memset(&sym, 0, sizeof(sym));
            sym.info.SizeOfStruct = sizeof(SYMBOL_INFO);
            sym.info.MaxNameLen = 255;
            DWORD64 disp = 0;
            if (fn && symFromAddr && symFromAddr(GetCurrentProcess(), (DWORD64)(size_t)fn, &disp, &sym.info))
                _snprintf(name, sizeof(name), "%s", sym.info.Name);
            name[sizeof(name) - 1] = 0;
            topLen += _snprintf(top + topLen, sizeof(top) - topLen, "%s\"%s\":[%.3f,%.1f]", k ? "," : "", name,
                (double)g_svProfBuiltinTicks[best] * msPerTick / frames, (double)g_svProfBuiltinCalls[best] / frames);
            if (topLen >= (int)sizeof(top) - 1)
                break;
            g_svProfBuiltinCalls[best] = 0;
        }
        top[sizeof(top) - 1] = 0;
        Com_Printf(15, "BO1_SVBUILTINS {\"level_time\":%d,\"top\":{%s}}\n", level.time, top);
        // bo1_mod_svprof 2: the 24 native functions most often under the server thread's instruction pointer outside
        // script, "category:function" with the share of those samples in percent
        if (g_svProfOn == 2)
        {
            static char pcNames[512][72];
            static unsigned int pcCounts[512];
            int numPc = 0;
            unsigned int pcTotal = 0;
            for (int i = 0; i < SVPROF_SAMPLE_SLOTS; ++i)
            {
                const unsigned int pc = s_svPcAddr[i];
                const unsigned int count = s_svPcCount[i];
                const int cat = s_svPcCat[i];
                s_svPcCount[i] = 0;
                s_svPcAddr[i] = 0;
                if (!pc || !count)
                    continue;
                pcTotal += count;
                const char *catName = cat < SVPROF_COUNT ? s_svProfNames[cat] : "?";
                char name[72];
                struct { SYMBOL_INFO info; char more[256]; } sym;
                memset(&sym, 0, sizeof(sym));
                sym.info.SizeOfStruct = sizeof(SYMBOL_INFO);
                sym.info.MaxNameLen = 255;
                DWORD64 disp = 0;
                if (symFromAddr && symFromAddr(GetCurrentProcess(), (DWORD64)pc, &disp, &sym.info))
                    _snprintf(name, sizeof(name), "%s:%s%s", catName, sym.info.Name, sym.info.Size && disp >= sym.info.Size ? "+past" : "");
                else
                    _snprintf(name, sizeof(name), "%s:0x%x", catName, pc);
                name[sizeof(name) - 1] = 0;
                for (char *c = name; *c; ++c)
                    if (*c == '"' || *c == '\\' || *c < ' ')
                        *c = '_';
                int k = 0;
                while (k < numPc && strcmp(pcNames[k], name))
                    ++k;
                if (k == numPc)
                {
                    if (numPc == 512)
                        continue;
                    strcpy(pcNames[numPc], name);
                    pcCounts[numPc++] = 0;
                }
                pcCounts[k] += count;
            }
            char pcTop[2048];
            int pcLen = 0;
            pcTop[0] = 0;
            for (int t = 0; t < 24 && pcTotal; ++t)
            {
                int best = -1;
                for (int k = 0; k < numPc; ++k)
                    if (pcCounts[k] && (best < 0 || pcCounts[k] > pcCounts[best]))
                        best = k;
                if (best < 0)
                    break;
                pcLen += _snprintf(pcTop + pcLen, sizeof(pcTop) - pcLen, "%s\"%s\":%.1f", t ? "," : "", pcNames[best], 100.0 * pcCounts[best] / pcTotal);
                if (pcLen >= (int)sizeof(pcTop) - 1)
                    break;
                pcCounts[best] = 0;
            }
            pcTop[sizeof(pcTop) - 1] = 0;
            Com_Printf(15, "BO1_SVPC {\"level_time\":%d,\"samples\":%u,\"top\":{%s}}\n", level.time, pcTotal, pcTop);
        }
        memset(g_svProfBuiltinTicks, 0, sizeof(g_svProfBuiltinTicks));
        memset(g_svProfBuiltinCalls, 0, sizeof(g_svProfBuiltinCalls));
    }
    winQpc0 = qpc.QuadPart;
    winTsc0 = now;
    frames = 0;
    sumMs = 0.0;
    memset(winTicks, 0, sizeof(winTicks));
    memset(winCalls, 0, sizeof(winCalls));
}
