#include "g_sp_player_state.h"
#include "g_scr_sp_players_x1.h"
#include "g_scr_sp_zombietron.h"
#include <game_mp/g_main_mp.h>
#include <cstring>
#include "g_sp_ext.h"
#include "g_sp_savegame.h"
#include <game_mp/g_scr_main_mp.h>
#include <game_mp/g_active_mp.h>
#include <game/turret.h>
#include <bgame/bg_animation.h>
#include <server/server.h>
#include <server_mp/sv_main_mp.h>
#include <bgame/bg_perks.h>
#include <clientscript/cscr_vm.h>

// zombies: SP names at 0x00B6DFE8, lookup 0x00584480. Preserve the exe's
// "scavanger" spelling. MP's numeric perk bits have different meanings.
static const char *s_spPerkNames[] =
{
    "specialty_longersprint", "specialty_unlimitedsprint", "specialty_scavanger",
    "specialty_fastreload", "specialty_bulletdamage", "specialty_bulletaccuracy",
    "specialty_flakjacket", "specialty_armorvest", "specialty_quickrevive",
    "specialty_altmelee", "specialty_rof", "specialty_extraammo",
    "specialty_endurance", "specialty_deadshot", "specialty_additionalprimaryweapon"
};

static unsigned int SP_PerkIndex(const char *name)
{
    for (unsigned int i = 0; i < ARRAY_COUNT(s_spPerkNames); ++i)
        if (!I_stricmp(name, s_spPerkNames[i]))
            return i;
    Scr_Error(va("Unknown perk: %s\n", name), false);
    return 0;
}

bool G_SP_HasPerk(gclient_s *client, const char *name)
{
    // zombies: hasperk tests either of the two bits (SP 0x007D90B0).
    return (G_SP_PlayerBuiltinState(client - level.clients).perks & (3u << (SP_PerkIndex(name) * 2))) != 0;
}

void G_SP_SetPerk(gclient_s *client, const char *name, bool enabled)
{
    // zombies: setperk / unsetperk update both PS and session (SP 0x007D8FF0 / 0x007D9170).
    const unsigned int mask = 3u << (SP_PerkIndex(name) * 2);
    unsigned int &perks = G_SP_PlayerBuiltinState(client - level.clients).perks;
    unsigned int &sessionPerks = G_ClientSpExt(client).sessionPerks;
    if (enabled)
    {
        perks |= mask;
        sessionPerks |= mask;
    }
    else
    {
        perks &= ~mask;
        sessionPerks &= ~mask;
    }
    // Keep KB's existing consumers and network layout for identically named perks.
    // SP-only perks remain in side storage unless they have an explicitly reserved wire bit.
    // zombies: endurance reaches shared/predicted movement (SP 0x0076058D).
    // Bit 52 is outside the 52 named MP perks, not an alias of an unrelated perk.
    if (!I_stricmp(name, "specialty_endurance"))
    {
        if (enabled)
        {
            client->ps.perks[1] |= BG_SP_ENDURANCE_PERK_MASK;
            client->sess.cs.perks[1] |= BG_SP_ENDURANCE_PERK_MASK;
        }
        else
        {
            client->ps.perks[1] &= ~BG_SP_ENDURANCE_PERK_MASK;
            client->sess.cs.perks[1] &= ~BG_SP_ENDURANCE_PERK_MASK;
        }
        return;
    }
    const unsigned int mp = BG_GetPerkIndexForName(name);
    if (mp < 52)
    {
        const unsigned int bit = 1u << (mp & 31);
        if (enabled)
        {
            client->ps.perks[mp >> 5] |= bit;
            client->sess.cs.perks[mp >> 5] |= bit;
        }
        else
        {
            client->ps.perks[mp >> 5] &= ~bit;
            client->sess.cs.perks[mp >> 5] &= ~bit;
        }
    }
}

void G_SP_GetPerks(gclient_s *client)
{
    Scr_MakeArray(SCRIPTINSTANCE_SERVER);
    for (unsigned int i = 0; i < ARRAY_COUNT(s_spPerkNames); ++i)
        if (G_SP_HasPerk(client, s_spPerkNames[i]))
        {
            Scr_AddString(s_spPerkNames[i], SCRIPTINSTANCE_SERVER);
            Scr_AddArray(SCRIPTINSTANCE_SERVER);
        }
}

static SPPlayerBuiltinState s_playerState[ARRAY_COUNT(g_clients)];
static bool s_disableGrenadeSuicide;

SPPlayerBuiltinState &G_SP_PlayerBuiltinState(unsigned int clientNum)
{
    iassert(clientNum < ARRAY_COUNT(g_clients));
    return s_playerState[clientNum];
}

void G_SP_ClearPlayerBuiltinState(gclient_s *client)
{
    const unsigned int clientNum = client - level.clients;
    iassert(clientNum < ARRAY_COUNT(g_clients));
    memset(&s_playerState[clientNum], 0, sizeof(s_playerState[clientNum]));
    G_SP_ClearPlayerX1State(clientNum); // lane x1 side state
    G_SP_ClearZombietronClientState(clientNum); // lane Z side state
}

void G_SP_ClearLevelBuiltinState()
{
    G_SP_ResetMissionFailed();
    G_SP_ClearSaveGameQueue();
    memset(s_playerState, 0, sizeof(s_playerState));
    s_disableGrenadeSuicide = false;
    G_SP_ClearLevelX1State(); // lane x1 side state
}

bool G_SP_ShouldEnterLastStand()
{
    // zombies: SP 0x004049F0, zombiemode branch (callers are zombie-only). "vs" enters, zombietron
    // (not registered in KB) would refuse. A solo game enters while its one active player is not in
    // last stand; otherwise the players not in last stand are counted twice (the exe's two loops both
    // add to the same total) and the player enters while that total exceeds 1. The non-zombie
    // means-of-death filter of the same function is not needed here.
    if (g_gametype && !I_stricmp(g_gametype->current.string, "vs"))
        return true;
    const dvar_s *zombietron = Dvar_FindVar("zombietron");
    if (zombietron && zombietron->current.enabled)
        return false;
    int active = 0, standing = 0;
    for (int i = 0; i < sv_maxclients->current.integer; ++i)
    {
        // SP client state 4 is KB's CS_ACTIVE (as in getnumconnectedplayers).
        if (svs.clients[i].header.state != CS_ACTIVE)
            continue;
        ++active;
        if (!svs.clients[i].gentity->client->lastStand)
            ++standing;
    }
    if (active == 1 && standing == 1)
        return true;
    for (int i = 0; i < sv_maxclients->current.integer; ++i)
    {
        if (svs.clients[i].header.state == CS_ACTIVE && !svs.clients[i].gentity->client->lastStand)
            ++standing;
    }
    return standing > 1;
}

void G_SP_PlayerEnterLastStand(gentity_s *ent, gentity_s *inflictor, gentity_s *attacker, int damage,
    unsigned int meansOfDeath, unsigned int weapon, float *dir, hitLocation_t hitLoc, int timeOffset)
{
    // zombies: SP 0x0055DB60. Unlike KB's MP last stand it neither resets the weapon state nor forces
    // a grenade throw.
    gclient_s *client = ent->client;
    Com_Printf(16, "bo1_death: time %d enter last stand client %d mod %d damage %d health %d\n", level.time,
        ent->s.number, meansOfDeath, damage, ent->health);
    client->lastStand = 1;
    G_GetClientState(ent->s.number)->lastStandStartTime = level.time + 500;
    if (client->ps.pm_flags & 2)
        BG_AnimScriptEvent(&g_pmove[client->ps.clientNum], ANIM_ET_CROUCH_TO_LASTSTAND, 0, 1);
    else if (client->ps.pm_flags & 1)
        BG_AnimScriptEvent(&g_pmove[client->ps.clientNum], ANIM_ET_PRONE_TO_LASTSTAND, 0, 1);
    else
        BG_AnimScriptEvent(&g_pmove[client->ps.clientNum], ANIM_ET_STAND_TO_LASTSTAND, 0, 1);
    if (client->ps.eFlags & 0x300)
    {
        gentity_s *turret = &g_entities[client->ps.viewlocked_entNum];
        if (turret->s.eType == 11)
            G_ClientStopUsingTurret(turret);
    }
    // SP 0x0055DC31 also takes a player out of a vehicle seat (eFlags 0x4000, 0x00440C80); Five has
    // no player vehicles and that exit is not ported here.
    Scr_PlayerLastStand(ent, inflictor, attacker, damage, meansOfDeath, weapon, dir, hitLoc, timeOffset);
}

bool G_SP_GrenadeSuicideDisabled()
{
    return s_disableGrenadeSuicide;
}

void G_SP_SetGrenadeSuicideDisabled(bool disabled)
{
    // zombies: G_LoadGame restores SP 0x01C08AE8 from the save (SP 0x007ECC37).
    s_disableGrenadeSuicide = disabled;
}

void G_SP_DisableGrenadeSuicide()
{
    s_disableGrenadeSuicide = true;
}
