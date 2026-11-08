// SP builtin table, lane G: AI builtins (getaiarray, getaispeciesarray, threat bias, spawners, ...).
// Searched after BO1Zombies's own tables and before the generated not-ported table (see
// src/game_sp/scr_sp_tables.h). Add a row per real port, keep the end marker last, then rerun
// `python tools/gen_sp_builtins.py` so the name's not-ported stub is dropped.
#include "scr_sp_tables.h"
#include "g_scr_sp_ai.h"
#include "actor_sp_ext.h"
#include <gfx_d3d/r_dvars.h>
#include "actor_sp_event_listeners.h"
#include "g_scr_sp_entity.h"
#include "g_scr_sp_ai_cmds.h"
#include <game/actor_script_cmd.h>
#include <game/actor_spawner.h>
#include <game/actor_threat.h>
#include <game/actor_navigation.h>
#include <game_mp/actor_mp.h>
#include <game_mp/g_main_mp.h>
#include <server/sv_game.h>
#include <game_mp/g_scr_main_mp.h>
#include <game_mp/g_spawn_mp.h>
#include <clientscript/cscr_vm.h>
#include <clientscript/cscr_stringlist.h>
#include <clientscript/scr_const.h>
#include <clientscript/cscr_variable.h>
#include <clientscript/cscr_compiler.h>
#include <universal/com_math.h>

// zombies: team flags for the AI array queries (SP 0x007f0840).
static int GScr_SP_TeamFlags(const char *team, const char *function)
{
    if (!I_stricmp(team, "axis"))
        return 2;
    if (!I_stricmp(team, "allies"))
        return 4;
    if (!I_stricmp(team, "neutral"))
        return 8;
    if (!I_stricmp(team, "all"))
        return 14;
    Scr_Error(va("unknown team '%s' in %s (should be axis, allies, or neutral)", team, function), 0);
    return 0;
}

// zombies: team argument walk from a first parameter index (SP 0x007f08d0); 0 if none given.
int GScr_SP_TeamFlagsFrom(unsigned int firstParam, const char *function)
{
    int teamFlags = 0;
    for (unsigned int i = firstParam; i < (unsigned int)Scr_GetNumParam(SCRIPTINSTANCE_SERVER); ++i)
        teamFlags |= GScr_SP_TeamFlags(Scr_GetString(i, SCRIPTINSTANCE_SERVER), function);
    return teamFlags;
}

// zombies: species lookup, including the all-species sentinel (SP 0x007f0920).
static int GScr_SP_Species(unsigned int name)
{
    if (name == scr_const.all)
        return MAX_AI_SPECIES;
    const unsigned int speciesNames[MAX_AI_SPECIES] =
    {
        g_actorSpScrConstHuman, scr_const.dog, g_actorSpScrConstZombie, g_actorSpScrConstZombieDog
    };
    for (int species = 0; species < MAX_AI_SPECIES; ++species)
    {
        if (name == speciesNames[species])
            return species;
    }
    Scr_Error(va("unknown species '%s' (should be human, dog, or all)",
        SL_ConvertToString(name, SCRIPTINSTANCE_SERVER)), 0);
    return MAX_AI_SPECIES;
}

// zombies: getaispeciesarray (SP 0x007f0b20).
static void G_f_getaispeciesarray()
{
    int teamFlags = 14;
    if (Scr_GetNumParam(SCRIPTINSTANCE_SERVER))
        teamFlags = GScr_SP_TeamFlags(Scr_GetString(0, SCRIPTINSTANCE_SERVER), "getaiarray");
    int species = MAX_AI_SPECIES;
    if (Scr_GetNumParam(SCRIPTINSTANCE_SERVER) > 1)
        species = GScr_SP_Species(Scr_GetConstString(1, SCRIPTINSTANCE_SERVER));
    Scr_MakeArray(SCRIPTINSTANCE_SERVER);
    for (actor_s *actor = Actor_FirstActor(teamFlags); actor; actor = Actor_NextActor(actor, teamFlags))
    {
        // SP +0xe24 is Physics.bIsAlive; +0xc62 is delayedDeath, not KB's byte at that offset.
        if (actor->Physics.bIsAlive && !actor->delayedDeath
            && (species == actor->species || species == MAX_AI_SPECIES))
        {
            Scr_AddEntity(actor->ent, SCRIPTINSTANCE_SERVER);
            Scr_AddArray(SCRIPTINSTANCE_SERVER);
        }
    }
}

// zombies: getaiarray (SP 0x007f0970; team argument walk 0x007f08d0).
static void G_f_getaiarray()
{
    int teamFlags = 0;
    for (int i = 0; i < Scr_GetNumParam(SCRIPTINSTANCE_SERVER); ++i)
        teamFlags |= GScr_SP_TeamFlags(Scr_GetString(i, SCRIPTINSTANCE_SERVER), "getaiarray");
    if (!teamFlags)
        teamFlags = 14;
    Scr_MakeArray(SCRIPTINSTANCE_SERVER);
    for (actor_s *actor = Actor_FirstActor(teamFlags); actor; actor = Actor_NextActor(actor, teamFlags))
    {
        if (actor->Physics.bIsAlive && !actor->delayedDeath && !Actor_IsDogSpecies(actor->species))
        {
            Scr_AddEntity(actor->ent, SCRIPTINSTANCE_SERVER);
            Scr_AddArray(SCRIPTINSTANCE_SERVER);
        }
    }
}

// zombies: common CodeSpawnerSpawn/CodeSpawnerForceSpawn body (SP 0x007f3250 / 0x007f33c0).
static void GScr_SP_CodeSpawnerSpawn(scr_entref_t entref, enumForceSpawn forceSpawn)
{
    gentity_s *ent = GetEntity(entref);
    if (ent->s.eType != ET_ACTOR_SPAWNER)
    {
        const char *function = forceSpawn == FORCE_SPAWN ? "CodeSpawnerForceSpawn" : "CodeSpawnerSpawn";
        Scr_Error(va("%s can only be called on actor spawners\nattempted to call %s on entity with name '%s' of type '%s' at (%.0f %.0f %.0f)\n",
            function, function, ent->targetname ? SL_ConvertToString(ent->targetname, SCRIPTINSTANCE_SERVER) : "<unnamed>",
            SL_ConvertToString(ent->classname, SCRIPTINSTANCE_SERVER),
            ent->r.currentOrigin[0], ent->r.currentOrigin[1], ent->r.currentOrigin[2]), 0);
    }
    if (ent->spawner.timestamp >= level.time)
        return;
    int noEnemyInfo = 0;
    if (Scr_GetNumParam(SCRIPTINSTANCE_SERVER))
        noEnemyInfo = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
    unsigned int targetname = 0;
    if (Scr_GetNumParam(SCRIPTINSTANCE_SERVER) > 1)
        targetname = Scr_GetConstString(1, SCRIPTINSTANCE_SERVER);
    if (ent->classname != scr_const.script_origin)
        ++g_spCodeSpawnCount;

    // zombies: SpawnActor's leading checks (SP 0x00526e50). Keep count-zero refusal here
    // until the SpawnActor owner also adds it for non-script callers.
    if (ai_disableSpawn->current.enabled)
    {
        Com_DPrintf(18, "Attempted spawn prevented by ai_disableSpawn.\n");
        return;
    }
    if (!ent->count)
    {
        Com_DPrintf(18, "^3Warning: SpawnActor( %s ) failed due to 0 count.\n",
            ent->targetname ? SL_ConvertToString(ent->targetname, SCRIPTINSTANCE_SERVER) : "<unnamed>");
        return;
    }
    gentity_s *spawn = SpawnActor(ent, targetname, forceSpawn, noEnemyInfo == 0);
    if (spawn)
    {
        ent->spawner.timestamp = level.time;
        // SP copies spawner+0x21c to svEntity+0x100 here (0x027f9808, stride 0x168).
        // KB has neither corresponding SP field;
        // the server/spawner owner must supply their storage and consumer.
        Scr_AddEntity(spawn, SCRIPTINSTANCE_SERVER);
    }
}

// zombies: codespawnerforcespawn (SP 0x007f33c0).
static void G_m_codespawnerforcespawn(scr_entref_t entref)
{
    GScr_SP_CodeSpawnerSpawn(entref, FORCE_SPAWN);
}

// zombies: codespawnerspawn (SP 0x007f3250).
static void G_m_codespawnerspawn(scr_entref_t entref)
{
    GScr_SP_CodeSpawnerSpawn(entref, CHECK_SPAWN);
}

// zombies: setengagementmindist (SP 0x007ce220).
static void G_m_setengagementmindist(scr_entref_t entref)
{
    actor_sp_ext_t *state = Actor_SP_Ext(Actor_Get(entref));
    state->engageMinDist = (float)Scr_GetFloat(0, SCRIPTINSTANCE_SERVER);
    state->engageMinFalloffDist = (float)Scr_GetFloat(1, SCRIPTINSTANCE_SERVER);
    if (state->engageMinDist < state->engageMinFalloffDist)
        Scr_Error(va("Min dist falloff must be <= min dist. [%f < %f]",
            state->engageMinDist, state->engageMinFalloffDist), 0);
}

// zombies: setengagementmaxdist (SP 0x007ce2c0).
static void G_m_setengagementmaxdist(scr_entref_t entref)
{
    actor_sp_ext_t *state = Actor_SP_Ext(Actor_Get(entref));
    state->engageMaxDist = (float)Scr_GetFloat(0, SCRIPTINSTANCE_SERVER);
    state->engageMaxFalloffDist = (float)Scr_GetFloat(1, SCRIPTINSTANCE_SERVER);
    if (state->engageMaxFalloffDist < state->engageMaxDist)
        Scr_Error(va("Max dist falloff must be >= max dist. [%f > %f]",
            state->engageMaxDist, state->engageMaxFalloffDist), 0);
}

// zombies: sentient method receiver checks (SP 0x0067b6a0 / 0x008195a0 / 0x00511750).
sentient_s *GScr_SP_GetSentient(scr_entref_t entref)
{
    if (!entref.classnum)
    {
        iassert(entref.entnum < g_scrEntNumLimit);
        if (g_entities[entref.entnum].sentient)
            return g_entities[entref.entnum].sentient;
    }
    Scr_ObjectError("not a sentient", SCRIPTINSTANCE_SERVER);
    return 0;
}

extern threat_bias_t g_threatBias;

// zombies: release the threat-bias group's owned string refs (SP 0x00512670).
// SP G_ShutdownGame (0x00607700) calls this before Scr_ShutdownSystem. Merely
// zeroing the names at the next Actor_InitThreatBiasGroups loses these refs.
void Actor_FreeThreatBiasGroups()
{
    for (unsigned int i = 0; i < ARRAY_COUNT(g_threatBias.groupName); ++i)
    {
        if (g_threatBias.groupName[i])
            Scr_SetString(&g_threatBias.groupName[i], 0, SCRIPTINSTANCE_SERVER);
    }
    memset(&g_threatBias, 0, sizeof(g_threatBias));
}

// zombies: threat bias group lookup (SP 0x005e2ac0).
static int Actor_SP_FindThreatBiasGroup(unsigned int name)
{
    for (int i = 0; i < g_threatBias.threatGroupCount; ++i)
    {
        if (g_threatBias.groupName[i] == name)
            return i;
    }
    return -1;
}

// zombies: isnotarget (SP 0x0067b6a0).
static void G_m_isnotarget(scr_entref_t entref)
{
    sentient_s *self = GScr_SP_GetSentient(entref);
    Scr_AddInt((self->ent->flags >> 2) & 1, SCRIPTINSTANCE_SERVER);
}

// zombies: setthreatbiasgroup (SP 0x008195a0).
static void G_m_setthreatbiasgroup(scr_entref_t entref)
{
    sentient_s *self = GScr_SP_GetSentient(entref);
    if (Scr_GetNumParam(SCRIPTINSTANCE_SERVER) != 1)
    {
        self->iThreatBiasGroupIndex = 0;
        return;
    }
    int group = Actor_SP_FindThreatBiasGroup(Scr_GetConstString(0, SCRIPTINSTANCE_SERVER));
    if (group >= 0)
    {
        self->iThreatBiasGroupIndex = group;
        return;
    }
    Scr_Error(va("Invalid threat bias group '%s'.\n", Scr_GetString(0, SCRIPTINSTANCE_SERVER)), 0);
}

// zombies: createthreatbiasgroup (SP 0x00819290).
static void G_f_createthreatbiasgroup()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_SERVER) != 1)
        Scr_ParamError(0, "createthreatbiasgroup [name]", SCRIPTINSTANCE_SERVER);
    unsigned int name = Scr_GetConstString(0, SCRIPTINSTANCE_SERVER);
    // SP checks capacity before looking for a duplicate (group zero is reserved).
    if (g_threatBias.threatGroupCount >= 16)
    {
        Com_PrintWarning(18, "Too many threat groups, can't create '%s'\n",
            SL_ConvertToString(name, SCRIPTINSTANCE_SERVER));
        return;
    }
    if (Actor_SP_FindThreatBiasGroup(name) >= 0)
        return;
    Scr_SetString(&g_threatBias.groupName[g_threatBias.threatGroupCount], name, SCRIPTINSTANCE_SERVER);
    ++g_threatBias.threatGroupCount;
}

// zombies: setthreatbias (SP 0x008193d0; table write 0x004e69d0).
static void G_f_setthreatbias()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_SERVER) != 3)
        Scr_ParamError(0, "setthreatbias [threatener] [threatened] [threat]", SCRIPTINSTANCE_SERVER);
    int threatener = Actor_SP_FindThreatBiasGroup(Scr_GetConstString(0, SCRIPTINSTANCE_SERVER));
    int threatened = Actor_SP_FindThreatBiasGroup(Scr_GetConstString(1, SCRIPTINSTANCE_SERVER));
    int threat = Scr_GetInt(2, SCRIPTINSTANCE_SERVER);
    if (threatener < 0)
    {
        Scr_Error(va("Invalid threat bias group '%s'.\n", Scr_GetString(0, SCRIPTINSTANCE_SERVER)), 0);
        return;
    }
    if (threatened < 0)
    {
        Scr_Error(va("Invalid threat bias group '%s'.\n", Scr_GetString(1, SCRIPTINSTANCE_SERVER)), 0);
        return;
    }
    g_threatBias.threatTable[threatener][threatened] = threat;
}

// zombies: getthreatbias (SP 0x00819320).
static void G_f_getthreatbias()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_SERVER) != 2)
        Scr_ParamError(0, "getthreatbias [group for] [group against]", SCRIPTINSTANCE_SERVER);
    int groupFor = Actor_SP_FindThreatBiasGroup(Scr_GetConstString(0, SCRIPTINSTANCE_SERVER));
    int groupAgainst = Actor_SP_FindThreatBiasGroup(Scr_GetConstString(1, SCRIPTINSTANCE_SERVER));
    if (groupFor < 0)
    {
        Scr_Error(va("Invalid threat bias group '%s'.\n", Scr_GetString(0, SCRIPTINSTANCE_SERVER)), 0);
        return;
    }
    if (groupAgainst < 0)
    {
        Scr_Error(va("Invalid threat bias group '%s'.\n", Scr_GetString(1, SCRIPTINSTANCE_SERVER)), 0);
        return;
    }
    Scr_AddInt(Actor_GetThreatBias(groupFor, groupAgainst), SCRIPTINSTANCE_SERVER);
}

// zombies: getclosestenemysqdist (SP 0x00511750).
static void G_m_getclosestenemysqdist(scr_entref_t entref)
{
    sentient_s *self = GScr_SP_GetSentient(entref);
    const int enemyTeams[6] = { 0, 2, 1, 0, 0, 0 };
    float closest = 100000000.0f; // SP 0x009ea3fc, also returned when no known enemy qualifies.
    int enemyTeam = enemyTeams[self->eTeam];
    if (!enemyTeam)
        return;
    int teamFlags = 1 << enemyTeam;
    float origin[3];
    Sentient_GetOrigin(self, origin);
    for (sentient_s *enemy = Sentient_FirstSentient(teamFlags); enemy; enemy = Sentient_NextSentient(enemy, teamFlags))
    {
        if (self->ent->actor->sentientInfo[enemy - level.sentients].lastKnownPosTime <= 0)
            continue;
        if ((enemy->ent->flags & 4) || Actor_CheckIgnore(self, enemy))
            continue;
        float enemyOrigin[3];
        Sentient_GetOrigin(enemy, enemyOrigin);
        float dx = enemyOrigin[0] - origin[0];
        float dy = enemyOrigin[1] - origin[1];
        float dz = enemyOrigin[2] - origin[2];
        // Keep the native sum order: z^2 + x^2, then y^2.
        float distance = (dz * dz + dx * dx) + dy * dy;
        if (distance < closest)
            closest = distance;
    }
    Scr_AddFloat(closest, SCRIPTINSTANCE_SERVER);
}

// mod (L43): bo1_mod_scriptperf 1. Scr_ModPatchSource (cscr_parser.cpp) rewrites two O(n^2) Five script loops to call
// these natives, which return exactly what the loops computed. Field keys are compiler canonical ids, saved at load.
static unsigned int s_modKeyIgnoreEnemyCount, s_modKeyAnimname;

void GScr_ModScriptPerfFields(scriptInstance_t inst)
{
    if (inst != SCRIPTINSTANCE_SERVER)
        return;
    const unsigned int ignore = SL_FindString("ignore_enemy_count", inst);
    const unsigned int animname = SL_FindString("animname", inst);
    s_modKeyIgnoreEnemyCount = ignore ? gScrCompilePub[inst].canonicalStrings[ignore] : 0;
    s_modKeyAnimname = animname ? gScrCompilePub[inst].canonicalStrings[animname] : 0;
}

// find-only read of an entity's script field (never creates one); 0 = undefined
static unsigned int GScr_ModFieldId(unsigned int object, unsigned int key)
{
    if (!object || !key)
        return 0;
    const unsigned int id = FindVariable(SCRIPTINSTANCE_SERVER, object, key);
    return id && GetValueType(SCRIPTINSTANCE_SERVER, id) != VAR_UNDEFINED ? id : 0;
}

// mod: maps\_zombiemode_utility::get_enemy_count (common_zombie_patch utility.gsc:51): GetAiSpeciesArray("axis", "all"),
// skip is_true(ignore_enemy_count), count isDefined(animname). The script's array_add copies the array per append (O(n^2)).
static void G_f_mod_getenemycount()
{
    int count = 0;
    for (actor_s *actor = Actor_FirstActor(2); actor; actor = Actor_NextActor(actor, 2))
    {
        if (!actor->Physics.bIsAlive || actor->delayedDeath) // getaispeciesarray's filter, species all
            continue;
        const unsigned int object = FindEntityId(SCRIPTINSTANCE_SERVER, actor->ent->s.number, 0, 0);
        if (const unsigned int id = GScr_ModFieldId(object, s_modKeyIgnoreEnemyCount))
        {
            // is_true: IsDefined(check) && check (a non-number would be a script cast error; counted as true)
            const unsigned int type = GetValueType(SCRIPTINSTANCE_SERVER, id);
            const VariableUnion &value = GetVariableValueAddress(SCRIPTINSTANCE_SERVER, id)->u;
            if (type == VAR_INTEGER ? value.intValue != 0 : type == VAR_FLOAT ? value.floatValue != 0.0f : true)
                continue;
        }
        if (GScr_ModFieldId(object, s_modKeyAnimname))
            ++count;
    }
    Scr_AddInt(count, SCRIPTINSTANCE_SERVER);
}

// mod (L55): self bo1_mod_runanimfreq() - zombie_run.gsc getRunAnimUpdateFrequency() under the bo1_mod_scriptperf
// rewrite: GetDvarFloat("ai_runAnimUpdateFrequency") (retail default 0.05, range 0.05-0.2). bo1_mod_ailod 1 (not exact,
// off by default): an actor farther than 1500 units from every player gets at least 0.2, so its idle run-loop poll
// (MoveMainLoop: choosePose, MoveStandCombatOverride's wait, trySideStep - pure polling for a running Kino zombie) runs
// every 4th frame instead of every frame. Cost: a run-cycle change (set_zombie_run_cycle / make_crawler set
// needs_run_update) reaches such a zombie up to 0.15 s late, and IsInCombat draws fewer RandomInts.
int g_modAiLodHits, g_modAiLodCalls;
static void G_m_mod_runanimfreq(scr_entref_t entref)
{
    extern int g_modAiLod;
    const gentity_s *self = GetEntity(entref);
    float value = atof(SV_Archived_Dvar_GetVariantString("ai_runAnimUpdateFrequency"));
    ++g_modAiLodCalls;
    if (g_modAiLod && value < 0.2f && self->actor)
    {
        bool isFar = true;
        for (int i = 0; i < level.maxclients && isFar; ++i)
        {
            const gentity_s *player = &g_entities[i];
            if (player->r.inuse && player->client
                && Vec3DistanceSq(player->r.currentOrigin, self->r.currentOrigin) < 1500.0f * 1500.0f)
                isFar = false;
        }
        if (isFar)
        {
            value = 0.2f;
            ++g_modAiLodHits;
        }
    }
    Scr_AddFloat(value, SCRIPTINSTANCE_SERVER);
}

// mod: self bo1_mod_sameenemycount(): find_flesh's crowd loop (common_zombie_patch spawner.gsc:3950-3967) over
// getaiarray("axis"): alive (health > 0), same defined favoriteenemy as self, within 225 of that enemy and farther
// than 525 from self. The loop ran per zombie every 1-3 s over every zombie (O(n^2) script).
static void G_m_mod_sameenemycount(scr_entref_t entref)
{
    const gentity_s *self = GetEntity(entref);
    int count = 0;
    if (self->actor && self->actor->pFavoriteEnemy.isDefined())
    {
        sentient_s *enemy = self->actor->pFavoriteEnemy.sentient();
        const float *enemyOrigin = enemy->ent->r.currentOrigin;
        for (actor_s *actor = Actor_FirstActor(2); actor; actor = Actor_NextActor(actor, 2))
        {
            // getaiarray's filter, then isalive
            if (!actor->Physics.bIsAlive || actor->delayedDeath || Actor_IsDogSpecies(actor->species)
                || actor->ent->health <= 0)
                continue;
            if (!actor->pFavoriteEnemy.isDefined() || actor->pFavoriteEnemy.sentient() != enemy)
                continue;
            if (Vec3DistanceSq(actor->ent->r.currentOrigin, enemyOrigin) < 225.0f * 225.0f
                && Vec3DistanceSq(actor->ent->r.currentOrigin, self->r.currentOrigin) > 525.0f * 525.0f)
                ++count;
        }
    }
    Scr_AddInt(count, SCRIPTINSTANCE_SERVER);
}

// mod (L52): self bo1_mod_nearestactordist() - distance to the nearest other live actor (horde mod _horde_unjam.gsc:
// a zombie without zombie collision turns it back on at the first spot where it does not overlap another). One C loop
// instead of a script loop over 600 zombies per call. 1000000 when there is none.
static void G_m_mod_nearestactordist(scr_entref_t entref)
{
    const gentity_s *self = GetEntity(entref);
    float best = 1000000.0f * 1000000.0f;
    for (actor_s *actor = Actor_FirstActor(2); actor; actor = Actor_NextActor(actor, 2))
    {
        if (actor->ent == self || !actor->Physics.bIsAlive || actor->delayedDeath || actor->ent->health <= 0)
            continue;
        const float d = Vec3DistanceSq(actor->ent->r.currentOrigin, self->r.currentOrigin);
        if (d < best)
            best = d;
    }
    Scr_AddFloat(sqrtf(best), SCRIPTINSTANCE_SERVER);
}

// mod: mapkit builtins (g_mapkit.cpp)
void G_Mapkit_f_editcount();
void G_Mapkit_f_layout();
void G_Mapkit_f_get();
void G_Mapkit_f_mark();
void G_Mapkit_f_visible();
void G_Mapkit_f_dump();
void G_Mapkit_f_selftestdone();
void G_Mapkit_f_graphcheck();
void G_Mapkit_f_catalog();
void G_Mapkit_f_light();
void G_Mapkit_f_lightuses();

const BuiltinFunctionDef g_sp_ai_functions[] =
{
    { "bo1_mapkit_editcount", G_Mapkit_f_editcount, 0 }, // mod (L46): mapkit layout access
    { "bo1_mapkit_layout", G_Mapkit_f_layout, 0 },
    { "bo1_mapkit_get", G_Mapkit_f_get, 0 },
    { "bo1_mapkit_mark", G_Mapkit_f_mark, 0 },
    { "bo1_mapkit_visible", G_Mapkit_f_visible, 0 },
    { "bo1_mapkit_dump", G_Mapkit_f_dump, 0 },
    { "bo1_mapkit_selftestdone", G_Mapkit_f_selftestdone, 0 },
    { "bo1_mapkit_graphcheck", G_Mapkit_f_graphcheck, 0 },
    { "bo1_mapkit_catalog", G_Mapkit_f_catalog, 0 },
    { "bo1_mapkit_light", G_Mapkit_f_light, 0 }, // mapkit-fix-3 mood lighting
    { "bo1_mapkit_lightuses", G_Mapkit_f_lightuses, 0 },
    { "bo1_mod_getenemycount", G_f_mod_getenemycount, 0 }, // mod (L43): only called by bo1_mod_scriptperf rewrites
    { "getaispeciesarray", G_f_getaispeciesarray, 0 },
    { "getaiarray", G_f_getaiarray, 0 },
    { "createthreatbiasgroup", G_f_createthreatbiasgroup, 0 },
    { "setthreatbias", G_f_setthreatbias, 0 },
    { "getthreatbias", G_f_getthreatbias, 0 },
    { "isnodeoccupied", G_f_isnodeoccupied, 0 },
    { "badplace_arc", G_f_badplace_arc, 0 },
    { "badplace_cylinder", G_f_badplace_cylinder, 0 },
    { "findpath", G_f_findpath, 0 },
    { "getanynodearray", G_f_getanynodearray, 0 },
    { "getweaponaccuracy", G_f_getweaponaccuracy, 0 },
    { nullptr, nullptr, 0 } // end marker, keep last
};
const unsigned int g_sp_ai_function_count = ARRAY_COUNT(g_sp_ai_functions) - 1;

// zombies: sethudwarningtype( type ) (SP 0x00805ff0): "banzai" 1, "banzai_grenadesuicide" 2, "zombie_friend" 3, else 0.
// NOT PORTED: sending it (SP actorState netfield, table 0x00a5c200) and the client reader (overhead name for type 3,
// SP 0x0050ea60 via CG_DrawFriendlyNames 0x004dd6c0).
static void G_m_sethudwarningtype(scr_entref_t entref)
{
    gentity_s *ent = 0;
    if (entref.classnum)
        Scr_ObjectError("not an entity", SCRIPTINSTANCE_SERVER);
    else
        ent = &g_entities[entref.entnum];
    if (!ent->actor)
    {
        Scr_Error(va("sethudwarningtype must be called on an AI, not on a '%s'",
            SL_ConvertToString(ent->classname, SCRIPTINSTANCE_SERVER)), SCRIPTINSTANCE_SERVER);
        return;
    }
    const char *type = Scr_GetString(0, SCRIPTINSTANCE_SERVER);
    int value = 0;
    if (!I_stricmp(type, "banzai"))
        value = 1;
    else if (!I_stricmp(type, "banzai_grenadesuicide"))
        value = 2;
    else if (!I_stricmp(type, "zombie_friend"))
        value = 3;
    Actor_SP_Ext(ent->actor)->hudWarningType = value;
}

// zombies: SP dev name table 0x00b77360 (40 entries), read by setzombiename while r_zombieNameAllowDevList is on.
static const char *s_zombieDevNames[40] = { "PLivingstone", "Astronaut Greer", "RElghazi", "ATabor", "GSpinrad",
    "ALivingston", "BPearson", "Cosmonaut Alex", "PonyGirl66", "JSimanello", "CCowell", "RHiga", "CAyers", "DLaufer",
    "Lamia", "Admiral Athena", "Nouriani", "Pink Unicorn", "JZielinski", "DSielke", "ducki02", "DKing", "SuzieQ64",
    "MAnthony", "CPierro", "ObusWolf", "Rezbox", "M Maestas", "HCheng", "WIp", "ABhura", "OGonzalez",
    "Spationaute Laura", "ALawton", "DDumlao", "DAnthony", "Zoe", "JDelgado", "BGlines", "Gordon" };
static int s_zombieDevNameIndex = -1; // SP 0x00b77404

// zombies: setzombiename( name ) (SP 0x008060e0). With r_zombieNameAllowDevList (default 1) the script's name is
// replaced by the dev table: first index irand(0, 40) (SP 0x00469ff0), then +1 per call, wrapping at 40. SP strcpy's
// into the 32-byte actorState name; KB bounds it. NOT PORTED: sending/drawing it (see sethudwarningtype).
static void G_m_setzombiename(scr_entref_t entref)
{
    gentity_s *ent = 0;
    if (entref.classnum)
        Scr_ObjectError("not an entity", SCRIPTINSTANCE_SERVER);
    else
        ent = &g_entities[entref.entnum];
    if (!ent->actor)
    {
        Scr_Error(va("SetZombieName must be called on an AI, not on a '%s'",
            SL_ConvertToString(ent->classname, SCRIPTINSTANCE_SERVER)), SCRIPTINSTANCE_SERVER);
        return;
    }
    const char *name = Scr_GetString(0, SCRIPTINSTANCE_SERVER);
    if (r_zombieNameAllowDevList && r_zombieNameAllowDevList->current.enabled)
    {
        if (s_zombieDevNameIndex < 0)
            s_zombieDevNameIndex = irand(0, 40);
        else
            s_zombieDevNameIndex = (s_zombieDevNameIndex + 1) % 40;
        name = s_zombieDevNames[s_zombieDevNameIndex];
    }
    I_strncpyz(Actor_SP_Ext(ent->actor)->zombieName, name, sizeof(Actor_SP_Ext(ent->actor)->zombieName));
}

// zombies: haspath() (SP 0x00802ad0): whether the AI has a path (Path_Exists on actor->Path, SP 0x00656c50 ->
// 0x00453550). Zombietron's spawner kills zombies that stay without one.
static void G_m_haspath(scr_entref_t entref)
{
    gentity_s *ent = 0;
    if (entref.classnum)
        Scr_ObjectError("not an entity", SCRIPTINSTANCE_SERVER);
    else
        ent = &g_entities[entref.entnum];
    if (!ent->actor)
    {
        Scr_Error(va("haspath must be called on an AI, not on a '%s'",
            SL_ConvertToString(ent->classname, SCRIPTINSTANCE_SERVER)), SCRIPTINSTANCE_SERVER);
        return;
    }
    Scr_AddInt(Path_Exists(&ent->actor->Path), SCRIPTINSTANCE_SERVER);
}

const BuiltinMethodDef g_sp_ai_methods[] =
{
    { "codespawnerforcespawn", G_m_codespawnerforcespawn, 0 },
    { "sethudwarningtype", G_m_sethudwarningtype, 0 },
    { "setzombiename", G_m_setzombiename, 0 },
    { "haspath", G_m_haspath, 0 },
    { "codespawnerspawn", G_m_codespawnerspawn, 0 },
    { "setengagementmindist", G_m_setengagementmindist, 0 },
    { "setengagementmaxdist", G_m_setengagementmaxdist, 0 },
    { "isnotarget", G_m_isnotarget, 0 },
    { "setthreatbiasgroup", G_m_setthreatbiasgroup, 0 },
    { "getclosestenemysqdist", G_m_getclosestenemysqdist, 0 },
    { "addaieventlistener", G_m_addaieventlistener, 0 },
    { "setdeathcontents", G_m_setdeathcontents, 0 },
    { "getaivelocity", G_m_getaivelocity, 0 },
    { "lookatentity", G_m_lookatentity, 0 },
    { "getthreatbiasgroup", G_m_getthreatbiasgroup, 0 },
    { "issuppressed", G_m_issuppressed, 0 },
    { "stopshoot", G_m_stopshoot, 0 },
    { "getturret", G_m_getturret, 0 },
    { "makefakeai", G_m_makefakeai, 0 },
    { "findbestcovernode", G_m_findbestcovernode, 0 },
    { "canshoot", G_m_canshoot, 0 },
    { "startactorreact", G_m_startactorreact, 0 },
    { "usecovernode", G_m_usecovernode, 0 },
    { "checkgrenadethrow", G_m_checkgrenadethrow, 0 },
    { "checkgrenadethrowpos", G_m_checkgrenadethrowpos, 0 },
    { "throwgrenade", G_m_throwgrenade, 0 },
    { "updateplayersightaccuracy", G_m_updateplayersightaccuracy, 0 },
    { "useturret", G_m_useturret, 0 },
    { "canuseturret", G_m_canuseturret, 0 },
    { "stopuseturret", G_m_stopuseturret, 0 },
    { "bo1_mod_runanimfreq", G_m_mod_runanimfreq, 0 }, // mod (L55): only called by bo1_mod_scriptperf rewrites
    { "bo1_mod_sameenemycount", G_m_mod_sameenemycount, 0 }, // mod (L43): only called by bo1_mod_scriptperf rewrites
    { "bo1_mod_nearestactordist", G_m_mod_nearestactordist, 0 }, // mod (L52): horde _horde_unjam.gsc
    { nullptr, nullptr, 0 } // end marker, keep last
};
const unsigned int g_sp_ai_method_count = ARRAY_COUNT(g_sp_ai_methods) - 1;
