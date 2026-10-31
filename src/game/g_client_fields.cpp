#include <game_sp/scr_sp_debug.h>
#include <win32/win_main.h>
#include "g_client_fields.h"
#include <clientscript/cscr_vm.h>
#include <clientscript/scr_const.h>
#include <clientscript/cscr_stringlist.h>
#include <client_mp/g_client_mp.h>
#include <universal/surfaceflags.h>
#include <server_mp/sv_init_mp.h>
#include <cgame_mp/cg_scr_main_mp.h>
#include <game_mp/g_spawn_mp.h>
#include <qcommon/common.h>
#include <game_sp/g_sp_ext.h>
#include <game_sp/g_scr_sp_zombietron.h>

static void __cdecl ClientScr_SetSpField(gclient_s *client, const client_fields_s *field);
static void __cdecl ClientScr_GetSpField(gclient_s *client, const client_fields_s *field);
static void __cdecl ClientScr_SetSpScoreField(gclient_s *client, const client_fields_s *field);

const char *g_scoreboardColumnNames[18] =
{
  "_invalid",
  "none",
  "kills",
  "deaths",
  "assists",
  "defends",
  "plants",
  "defuses",
  "returns",
  "captures",
  "destructions",
  "kdratio",
  "survived",
  "stabs",
  "tomahawks",
  "humiliated",
  "x2score",
  "headshots"
};

const client_fields_s fields[] =
{
  {
    "sessionteam",
    0,
    { 0 },
    F_STRING,
    0u,
    ClientScr_SetSessionTeam,
    ClientScr_GetSessionTeam
  },
  {
    "ffateam",
    0,
    { 0 },
    F_STRING,
    0u,
    ClientScr_SetFFATeam,
    ClientScr_GetFFATeam
  },
  { "name", 0, { 0 }, F_LSTRING, 0u, ClientScr_ReadOnly, ClientScr_GetName },
  { "maxhealth", 10068, { 4 }, F_INT, 0u, ClientScr_SetMaxHealth, NULL },
  {
    "headiconteam",
    0,
    { 0 },
    F_STRING,
    0u,
    ClientScr_SetHeadIconTeam,
    ClientScr_GetHeadIconTeam
  },
  { "hasspyplane", 10692, { 4 }, F_INT, 0u, NULL, NULL },
  { "hassatellite", 10696, { 4 }, F_INT, 0u, NULL, NULL },
  { "disallowvehicleusage", 10708, { 4 }, F_INT, 0u, NULL, NULL },
  { "kills", 10280, { 4 }, F_INT, 0u, ClientScr_SetKills, NULL },
  { "deaths", 10288, { 4 }, F_INT, 0u, ClientScr_SetDeaths, NULL },
  { "assists", 10284, { 4 }, F_INT, 0u, ClientScr_SetAssists, NULL },
  { "defends", 10336, { 4 }, F_INT, 0u, ClientScr_SetDefends, NULL },
  { "plants", 10340, { 4 }, F_INT, 0u, ClientScr_SetPlants, NULL },
  { "defuses", 10344, { 4 }, F_INT, 0u, ClientScr_SetDefuses, NULL },
  { "returns", 10348, { 4 }, F_INT, 0u, ClientScr_SetReturns, NULL },
  { "captures", 10352, { 4 }, F_INT, 0u, ClientScr_SetCaptures, NULL },
  { "destructions", 10356, { 4 }, F_INT, 0u, ClientScr_SetDestructions, NULL },
  { "survived", 10364, { 4 }, F_INT, 0u, ClientScr_SetSurvived, NULL },
  { "stabs", 10368, { 4 }, F_INT, 0u, ClientScr_SetStabs, NULL },
  { "tomahawks", 10372, { 4 }, F_INT, 0u, ClientScr_SetTomahawks, NULL },
  { "humiliated", 10376, { 4 }, F_INT, 0u, ClientScr_SetHumiliated, NULL },
  { "x2score", 10380, { 4 }, F_INT, 0u, ClientScr_SetX2Score, NULL },
  { "headshots", 10384, { 4 }, F_INT, 0u, ClientScr_SetHeadshots, NULL },
  { "killcamentity", 9904, { 4 }, F_INT, 0u, ClientScr_SetKillCamEntity, NULL },
  {
    "killcamtargetentity",
    9908,
    { 4 },
    F_INT,
    0u,
    ClientScr_SetKillCamTargetEntity,
    NULL
  },
  { "score", 10276, { 4 }, F_INT, 0u, ClientScr_SetScore, NULL },
  {
    "sessionstate",
    0,
    { 0 },
    F_STRING,
    0u,
    ClientScr_SetSessionState,
    ClientScr_GetSessionState
  },
  {
    "statusicon",
    0,
    { 0 },
    F_STRING,
    0u,
    ClientScr_SetStatusIcon,
    ClientScr_GetStatusIcon
  },
  {
    "spectatorclient",
    9900,
    { 4 },
    F_INT,
    0u,
    ClientScr_SetSpectatorClient,
    NULL
  },
  {
    "archivetime",
    9912,
    { 4 },
    F_FLOAT,
    0u,
    ClientScr_SetArchiveTime,
    ClientScr_GetArchiveTime
  },
  {
    "psoffsettime",
    10312,
    { 4 },
    F_INT,
    0u,
    ClientScr_SetPSOffsetTime,
    ClientScr_GetPSOffsetTime
  },
  { "pers", 9916, { 4 }, F_OBJECT, 0u, ClientScr_ReadOnly, NULL },
  { "usingturret", 224, { 4 }, F_BITFLAG, 768u, ClientScr_ReadOnly, NULL },
  { "usingvehicle", 224, { 4 }, F_BITFLAG, 16384u, ClientScr_ReadOnly, NULL },
  { "vehicleposition", 1092, { 4 }, F_INT, 0u, ClientScr_ReadOnly, NULL },
  {
    "headicon",
    0,
    { 0 },
    F_STRING,
    0u,
    ClientScr_SetHeadIcon,
    ClientScr_GetHeadIcon
  },
  { "divetoprone", 12, { 4 }, F_BITFLAG, 4194304u, NULL, NULL },
  { "sprinting", 12, { 4 }, F_BITFLAG, 32768u, NULL, NULL },
  {
    "groundentity",
    144,
    { 4 },
    F_ENTITY,
    0u,
    ClientScr_ReadOnly,
    ClientScr_GetEntityFromIndex
  },
  {
    "viewlockedentity",
    1088,
    { 2 },
    F_ENTITY,
    0u,
    ClientScr_ReadOnly,
    ClientScr_GetEntityFromIndex
  },
  {
    "groundsurfacetype",
    0,
    { 0 },
    F_STRING,
    0u,
    ClientScr_ReadOnly,
    ClientScr_GetGroundType
  },
  // zombies: SP getter uses CS_DisplayName(client +0x1ae8, 3) -> sess.cs
  // (SP row 0x00a527a8, getter 0x007d5ac0, read-only setter 0x007d59a0).
  { "playername", 0, { 0 }, F_LSTRING, 0u, ClientScr_ReadOnly, ClientScr_GetName },
  // zombies: SP +0x1ba4 -> gclientSpExt::downs (SP 0x00a527fc).
  { "downs", offsetof(gclientSpExt, downs), { 4 }, F_INT, 0u, ClientScr_SetSpScoreField, ClientScr_GetSpField },
  // zombies: SP +0x1ba8 -> gclientSpExt::revives (SP 0x00a52818).
  { "revives", offsetof(gclientSpExt, revives), { 4 }, F_INT, 0u, ClientScr_SetSpScoreField, ClientScr_GetSpField },
  // zombies: SP +0xc, mask 0x02000000 -> ps.pm_flags (SP 0x00a52850).
  { "nolean", offsetof(gclient_s, ps.pm_flags), { 4 }, F_BITFLAG, 0x02000000u, NULL, NULL },
  // zombies: SP +0xc, mask 0x10000000 -> ps.pm_flags (SP 0x00a5286c).
  { "nopronerotation", offsetof(gclient_s, ps.pm_flags), { 4 }, F_BITFLAG, 0x10000000u, NULL, NULL },
  // zombies: SP +0x1c80 -> gclientSpExt::lookatent; read-only (SP 0x00a52a48).
  { "lookatent", offsetof(gclientSpExt, lookatent), { 4 }, F_ENTHANDLE, 0u, ClientScr_ReadOnly, ClientScr_GetSpField },
  { NULL, 0, { 0 }, F_INT, 0u, NULL, NULL }
};

static void __cdecl ClientScr_SetSpField(gclient_s *client, const client_fields_s *field)
{
    GScr_SetGenericField((unsigned __int8 *)&G_ClientSpExt(client), field->type, field->ofs, field->whichbits);
}

static void __cdecl ClientScr_GetSpField(gclient_s *client, const client_fields_s *field)
{
    GScr_GetGenericField((unsigned __int8 *)&G_ClientSpExt(client), field->type, field->ofs, field->whichbits);
}

// zombies: downs and revives also reach zombietron's HUD as scoreboard columns (SB_ZT_BOOSTERS / SB_ZT_LIVES,
// g_scr_sp_zombietron.h); SP sends them as clientState score.downs / score.revives.
static void __cdecl ClientScr_SetSpScoreField(gclient_s *client, const client_fields_s *field)
{
    ClientScr_SetSpField(client, field);
    if (field->ofs == offsetof(gclientSpExt, downs))
        G_SP_SetScoreColumn(client, SB_ZT_BOOSTERS, G_ClientSpExt(client).downs);
    else
        G_SP_SetScoreColumn(client, SB_ZT_LIVES, G_ClientSpExt(client).revives);
}

void __cdecl ClientScr_ReadOnly(gclient_s *pSelf, const client_fields_s *pField)
{
    const char *v2; // eax

    if ( !pSelf && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 27, 0, "%s", "pSelf") )
        __debugbreak();
    if ( !pField
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 28, 0, "%s", "pField") )
    {
        __debugbreak();
    }
    v2 = va("player field %s is read-only", pField->name);
    Scr_Error(v2, 0);
}

void __cdecl ClientScr_SetSessionTeam(gclient_s *pSelf, const client_fields_s *__formal)
{
    char *v1; // eax
    const char *v2; // eax
    unsigned __int16 newTeam; // [esp+0h] [ebp-4h]

    if ( !pSelf && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 38, 0, "%s", "pSelf") )
        __debugbreak();
    newTeam = (unsigned __int16)Scr_GetConstString(0, SCRIPTINSTANCE_SERVER);
    if ( newTeam == scr_const.axis )
    {
        pSelf->sess.cs.team = TEAM_AXIS;
    }
    else if ( newTeam == scr_const.allies )
    {
        pSelf->sess.cs.team = TEAM_ALLIES;
    }
    else if ( newTeam == scr_const.spectator )
    {
        pSelf->sess.cs.team = TEAM_SPECTATOR;
    }
    else if ( newTeam == scr_const.none )
    {
        pSelf->sess.cs.team = TEAM_FREE;
    }
    else
    {
        v1 = SL_ConvertToString(newTeam, SCRIPTINSTANCE_SERVER);
        v2 = va("'%s' is an illegal sessionteam string. Must be allies, axis, none, or spectator.", v1);
        Scr_Error(v2, 0);
    }
    if ( pSelf - level.clients >= level.maxclients )
        Scr_Error("client is not pointing to the level.clients array", 0);
    //BLOPS_NULLSUB();
    ClientUserinfoChanged(pSelf - level.clients);
    CalculateRanks();
}

void __cdecl ClientScr_SetFFATeam(gclient_s *pSelf, const client_fields_s *__formal)
{
    char *v1; // eax
    const char *v2; // eax
    unsigned __int16 newTeam; // [esp+0h] [ebp-4h]

    if ( !pSelf && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 70, 0, "%s", "pSelf") )
        __debugbreak();
    newTeam = (unsigned __int16)Scr_GetConstString(0, SCRIPTINSTANCE_SERVER);
    if ( newTeam == scr_const.axis )
    {
        pSelf->sess.cs.ffaTeam = TEAM_FFA_AXIS;
    }
    else if ( newTeam == scr_const.allies )
    {
        pSelf->sess.cs.ffaTeam = TEAM_FFA_ALLIES;
    }
    else if ( newTeam == scr_const.none || newTeam == scr_const.spectator )
    {
        pSelf->sess.cs.ffaTeam = TEAM_FFA_NONE;
    }
    else
    {
        v1 = SL_ConvertToString(newTeam, SCRIPTINSTANCE_SERVER);
        v2 = va("'%s' is an illegal ffateam string. Must be allies or axis.", v1);
        Scr_Error(v2, 0);
    }
}

void __cdecl ClientScr_GetFFATeam(gclient_s *pSelf, const client_fields_s *__formal)
{
    ffa_team_t ffaTeam; // [esp+0h] [ebp-4h]

    if ( !pSelf && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 86, 0, "%s", "pSelf") )
        __debugbreak();
    ffaTeam = pSelf->sess.cs.ffaTeam;
    if ( ffaTeam == TEAM_FFA_AXIS )
    {
        Scr_AddConstString(scr_const.axis, SCRIPTINSTANCE_SERVER);
    }
    else if ( ffaTeam == TEAM_FFA_ALLIES )
    {
        Scr_AddConstString(scr_const.allies, SCRIPTINSTANCE_SERVER);
    }
    else
    {
        Scr_AddConstString(scr_const.none, SCRIPTINSTANCE_SERVER);
    }
}

void __cdecl ClientScr_GetName(gclient_s *pSelf, const client_fields_s *__formal)
{
    char *v1; // eax

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 108, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    v1 = (char *)CS_DisplayName(&pSelf->sess.cs, 3);
    Scr_AddString(v1, SCRIPTINSTANCE_SERVER);
}

void __cdecl ClientScr_GetGroundType(gclient_s *pSelf, const client_fields_s *__formal)
{
    char *groundType; // [esp+0h] [ebp-4h]

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 115, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( pSelf->ps.groundType )
    {
        groundType = (char *)Com_SurfaceTypeToName(pSelf->ps.groundType);
        Scr_AddString(groundType, SCRIPTINSTANCE_SERVER);
    }
}

void __cdecl ClientScr_GetSessionTeam(gclient_s *pSelf, const client_fields_s *__formal)
{
    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 126, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    switch ( pSelf->sess.cs.team )
    {
        case TEAM_FREE:
            Scr_AddConstString(scr_const.none, SCRIPTINSTANCE_SERVER);
            break;
        case TEAM_AXIS:
            Scr_AddConstString(scr_const.axis, SCRIPTINSTANCE_SERVER);
            break;
        case TEAM_ALLIES:
            Scr_AddConstString(scr_const.allies, SCRIPTINSTANCE_SERVER);
            break;
        case TEAM_SPECTATOR:
            Scr_AddConstString(scr_const.spectator, SCRIPTINSTANCE_SERVER);
            break;
        default:
            return;
    }
}

int __cdecl CScr_GetColumnTypeByName(const char *typeName)
{
    int i; // [esp+14h] [ebp-4h]

    for ( i = 0; i < 18; ++i )
    {
        if ( !strcmp(typeName, g_scoreboardColumnNames[i]) )
            return i;
    }
    return 0;
}

const char *__cdecl CScr_GetColumnNameByType(scoreboardColumnType_t columnType)
{
    if ( (unsigned int)columnType < SB_TYPE_ASSISTS )
        return g_scoreboardColumnNames[columnType];
    Com_PrintWarning(1, "There is no name for scoreboard column type %i.", columnType);
    return g_scoreboardColumnNames[0];
}

void __cdecl ClientScr_SetSessionState(gclient_s *pSelf, const client_fields_s *__formal)
{
    char *v1; // eax
    const char *v2; // eax
    unsigned __int16 newState; // [esp+0h] [ebp-4h]

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 203, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( pSelf->sess.connected == CON_DISCONNECTED
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    204,
                    0,
                    "%s",
                    "pSelf->sess.connected != CON_DISCONNECTED") )
    {
        __debugbreak();
    }
    newState = (unsigned __int16)Scr_GetConstString(0, SCRIPTINSTANCE_SERVER);
    if ( newState == scr_const.playing )
    {
        pSelf->sess.sessionState = SESS_STATE_PLAYING;
    }
    else if ( newState == scr_const.dead )
    {
        pSelf->sess.sessionState = SESS_STATE_DEAD;
    }
    else if ( newState == scr_const.spectator )
    {
        pSelf->sess.sessionState = SESS_STATE_SPECTATOR;
    }
    else if ( newState == scr_const.intermission )
    {
        pSelf->ps.eFlags ^= 2u;
        pSelf->sess.sessionState = SESS_STATE_INTERMISSION;
        if ( Demo_IsRecording() )
            Demo_End(0);
    }
    else
    {
        v1 = SL_ConvertToString(newState, SCRIPTINSTANCE_SERVER);
        v2 = va("'%s' is an illegal sessionstate string. Must be playing, dead, spectator, or intermission.", v1);
        Scr_Error(v2, 0);
    }
}

void __cdecl ClientScr_GetSessionState(gclient_s *pSelf, const client_fields_s *__formal)
{
    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 244, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( pSelf->sess.connected == CON_DISCONNECTED
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    245,
                    0,
                    "%s",
                    "pSelf->sess.connected != CON_DISCONNECTED") )
    {
        __debugbreak();
    }
    switch ( pSelf->sess.sessionState )
    {
        case SESS_STATE_PLAYING:
            Scr_AddConstString(scr_const.playing, SCRIPTINSTANCE_SERVER);
            break;
        case SESS_STATE_DEAD:
            Scr_AddConstString(scr_const.dead, SCRIPTINSTANCE_SERVER);
            break;
        case SESS_STATE_SPECTATOR:
            Scr_AddConstString(scr_const.spectator, SCRIPTINSTANCE_SERVER);
            break;
        case SESS_STATE_INTERMISSION:
            Scr_AddConstString(scr_const.intermission, SCRIPTINSTANCE_SERVER);
            break;
        default:
            return;
    }
}

void __cdecl ClientScr_SetMaxHealth(gclient_s *pSelf, const client_fields_s *__formal)
{
    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 278, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( (unsigned int)(pSelf - level.clients) >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    279,
                    0,
                    "pSelf - level.clients doesn't index level.maxclients\n\t%i not in [0, %i)",
                    pSelf - level.clients,
                    level.maxclients) )
    {
        __debugbreak();
    }
    pSelf->sess.maxHealth = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
    if ( pSelf->sess.maxHealth < 1 )
        pSelf->sess.maxHealth = 1;
    if ( pSelf->ps.stats[0] > pSelf->sess.maxHealth )
        pSelf->ps.stats[0] = pSelf->sess.maxHealth;
    g_entities[pSelf - level.clients].health = pSelf->ps.stats[0];
    pSelf->ps.stats[2] = pSelf->sess.maxHealth;
}

void __cdecl ClientScr_SetScore(gclient_s *pSelf, const client_fields_s *__formal)
{
    VariableUnion v1; // [esp+0h] [ebp-10h]
    int intValue; // [esp+4h] [ebp-Ch]
    VariableUnion v3; // [esp+8h] [ebp-8h]
    int score; // [esp+Ch] [ebp-4h]

    score = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 306, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( (unsigned int)(pSelf - level.clients) >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    307,
                    0,
                    "pSelf - level.clients doesn't index level.maxclients\n\t%i not in [0, %i)",
                    pSelf - level.clients,
                    level.maxclients) )
    {
        __debugbreak();
    }
    if ( zombiemode && zombiemode->current.enabled )
    {
        // zombies: SP ClientScr_SetScore (SP 0x007D5CD0) has no +-30000 clamp. It rounds the score up to a
        // multiple of 10, stores that /10 through G_GetClientState (SP 0x005DA470 = &level.clients[n].sess.cs)
        // into cs.score.score (+0xa0), then stores the raw score to gclient+0x1b88 - the same int
        // (sess.cs at +0x1ae8), so the /10 store is dead and the field holds the raw value. The netfield
        // is widened to match (MSG_GetNetFieldList, SP score.score is an int delta, type -79).
        intValue = score;
    }
    else if ( score < 0 )
    {
        if ( score < -30000 )
            v1.intValue = -30000;
        else
            v1.intValue = score;
        intValue = v1.intValue;
    }
    else
    {
        if ( score > 30000 )
            v3.intValue = 30000;
        else
            v3.intValue = score;
        intValue = v3.intValue;
    }
    if (Sys_IsHeadless() && Dvar_GetBool("bo1_testclient"))
        Com_Printf(16, "bo1_testclient: score %d -> %d at %s time %d\n",
            pSelf->sess.cs.score.score, intValue,
            Scr_SP_DescribeCodePos(SCRIPTINSTANCE_SERVER, gScrVmPub[SCRIPTINSTANCE_SERVER].function_frame->fs.pos), level.time);
    pSelf->sess.cs.score.score = intValue;
    CalculateRanks();
}

void __cdecl ClientScr_SetKills(gclient_s *pSelf, const client_fields_s *__formal)
{
    pSelf->sess.cs.score.kills = ClientScr_SetColumnValue(pSelf, SB_TYPE_KILLS).intValue;
}

VariableUnion __cdecl ClientScr_SetColumnValue(gclient_s *pSelf, scoreboardColumnType_t columnType)
{
    int columnIndex; // [esp+0h] [ebp-8h]
    int value; // [esp+4h] [ebp-4h]

    value = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
    pSelf->sess.scoreboardColumnCache[columnType] = value;
    if ( columnType == SB_TYPE_X2SCORE )
        value /= 10;
    columnIndex = ClientScr_GetScoreboardColumnIndexByType(columnType);
    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 354, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( columnIndex >= 0 )
        pSelf->sess.cs.score.scoreboardColumns[columnIndex] = value;
    return (VariableUnion)value;
}

int __cdecl ClientScr_GetScoreboardColumnIndexByType(scoreboardColumnType_t columnType)
{
    int i; // [esp+0h] [ebp-8h]
    MatchState *matchState; // [esp+4h] [ebp-4h]

    matchState = G_GetMatchState();
    if ( !matchState
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 336, 0, "%s", "matchState") )
    {
        __debugbreak();
    }
    for ( i = 0; i < 4; ++i )
    {
        if ( matchState->unarchivedState.scoreboardColumnTypes[i] == columnType )
            return i;
    }
    return -1;
}

void G_SP_SetScoreColumn(gclient_s *client, scoreboardColumnType_t columnType, int value)
{
    const int columnIndex = ClientScr_GetScoreboardColumnIndexByType(columnType);
    if (columnIndex >= 0)
        client->sess.cs.score.scoreboardColumns[columnIndex] = value;
}

void ClientScr_SetDeaths(gclient_s *pSelf, const client_fields_s *__formal)
{
    pSelf->sess.cs.score.deaths = ClientScr_SetColumnValue(pSelf, SB_TYPE_DEATHS).intValue;
}

void __cdecl ClientScr_SetAssists(gclient_s *pSelf, const client_fields_s *__formal)
{
    pSelf->sess.cs.score.assists =
        ClientScr_SetColumnValue(pSelf, SB_TYPE_ASSISTS).intValue;
}

void __cdecl ClientScr_SetDefends(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_DEFENDS);
}

void __cdecl ClientScr_SetPlants(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_PLANTS);
}

void __cdecl ClientScr_SetDefuses(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_DEFUSES);
}

void __cdecl ClientScr_SetReturns(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_RETURNS);
}

void __cdecl ClientScr_SetCaptures(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_CAPTURES);
}

void __cdecl ClientScr_SetDestructions(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_DESTRUCTIONS);
}

void __cdecl ClientScr_SetSurvived(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_SURVIVED);
}

void __cdecl ClientScr_SetStabs(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_STABS);
}

void __cdecl ClientScr_SetTomahawks(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_TOMAHAWKS);
}

void __cdecl ClientScr_SetHumiliated(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_HUMILIATED);
}

void __cdecl ClientScr_SetX2Score(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_X2SCORE);
}

void __cdecl ClientScr_SetHeadshots(gclient_s *pSelf, const client_fields_s *__formal)
{
    ClientScr_SetColumnValue(pSelf, SB_TYPE_HEADSHOTS);
}


void __cdecl ClientScr_SetSpectatorClient(gclient_s *pSelf, const client_fields_s *__formal)
{
    int iNewSpectatorClient; // [esp+0h] [ebp-4h]

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 524, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    iNewSpectatorClient = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
    if ( iNewSpectatorClient < -1 || iNewSpectatorClient >= 32 )
        Scr_Error("spectatorclient can only be set to -1, or a valid client number", 0);
    pSelf->sess.forceSpectatorClient = iNewSpectatorClient;
}

void __cdecl ClientScr_SetKillCamEntity(gclient_s *pSelf, const client_fields_s *__formal)
{
    int iNewKillCamEntity; // [esp+0h] [ebp-4h]

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 545, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    iNewKillCamEntity = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
    if ( iNewKillCamEntity < -1 || iNewKillCamEntity >= 1024 )
        Scr_Error("killcamentity can only be set to -1, or a valid entity number", 0);
    pSelf->sess.killCamEntity = iNewKillCamEntity;
}

void __cdecl ClientScr_SetKillCamTargetEntity(gclient_s *pSelf, const client_fields_s *__formal)
{
    int iNewKillCamEntity; // [esp+0h] [ebp-4h]

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 559, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    iNewKillCamEntity = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
    if ( iNewKillCamEntity < -1 || iNewKillCamEntity >= 1024 )
        Scr_Error("killcamentity can only be set to -1, or a valid entity number", 0);
    pSelf->sess.killCamTargetEntity = iNewKillCamEntity;
}

void __cdecl ClientScr_SetStatusIcon(gclient_s *pSelf, const client_fields_s *__formal)
{
    char *pszIcon; // [esp+0h] [ebp-4h]

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 581, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    pszIcon = Scr_GetString(0, SCRIPTINSTANCE_SERVER);
    pSelf->sess.cs.score.status_icon = GScr_GetStatusIconIndex(pszIcon);
}

void __cdecl ClientScr_GetStatusIcon(gclient_s *pSelf, const client_fields_s *__formal)
{
    char szConfigString[1028]; // [esp+0h] [ebp-408h] BYREF

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 595, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( pSelf->sess.cs.score.status_icon > 8u
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    597,
                    0,
                    "%s",
                    "pSelf->sess.cs.score.status_icon >= 0 && pSelf->sess.cs.score.status_icon <= MAX_STATUS_ICONS") )
    {
        __debugbreak();
    }
    if ( pSelf->sess.cs.score.status_icon )
    {
        SV_GetConfigstring(pSelf->sess.cs.score.status_icon + 3091, szConfigString, 1024);
        Scr_AddString(szConfigString, SCRIPTINSTANCE_SERVER);
    }
    else
    {
        Scr_AddString((char *)"", SCRIPTINSTANCE_SERVER);
    }
}

void __cdecl ClientScr_SetHeadIcon(gclient_s *pSelf, const client_fields_s *__formal)
{
    gentity_s *pEnt; // [esp+0h] [ebp-8h]
    char *pszIcon; // [esp+4h] [ebp-4h]

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 622, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( (unsigned int)(pSelf - level.clients) >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    623,
                    0,
                    "pSelf - level.clients doesn't index level.maxclients\n\t%i not in [0, %i)",
                    pSelf - level.clients,
                    level.maxclients) )
    {
        __debugbreak();
    }
    pEnt = &g_entities[pSelf - level.clients];
    pszIcon = Scr_GetString(0, SCRIPTINSTANCE_SERVER);
    pEnt->s.iHeadIcon = GScr_GetHeadIconIndex(pszIcon);
}

void __cdecl ClientScr_GetHeadIcon(gclient_s *pSelf, const client_fields_s *__formal)
{
    char szConfigString[1024]; // [esp+0h] [ebp-408h] BYREF
    gentity_s *pEnt; // [esp+404h] [ebp-4h]

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 643, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( (unsigned int)(pSelf - level.clients) >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    644,
                    0,
                    "pSelf - level.clients doesn't index level.maxclients\n\t%i not in [0, %i)",
                    pSelf - level.clients,
                    level.maxclients) )
    {
        __debugbreak();
    }
    pEnt = &g_entities[pSelf - level.clients];
    if ( pEnt->s.iHeadIcon )
    {
        if ( pEnt->s.iHeadIcon <= 0xFu )
        {
            SV_GetConfigstring(pEnt->s.iHeadIcon + 3099, szConfigString, 1024);
            Scr_AddString(szConfigString, SCRIPTINSTANCE_SERVER);
        }
    }
    else
    {
        Scr_AddString((char *)"", SCRIPTINSTANCE_SERVER);
    }
}

void __cdecl ClientScr_SetHeadIconTeam(gclient_s *pSelf, const client_fields_s *__formal)
{
    char *v1; // eax
    const char *v2; // eax
    gentity_s *pEnt; // [esp+0h] [ebp-8h]
    unsigned __int16 sTeam; // [esp+4h] [ebp-4h]

    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 671, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( (unsigned int)(pSelf - level.clients) >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    672,
                    0,
                    "pSelf - level.clients doesn't index level.maxclients\n\t%i not in [0, %i)",
                    pSelf - level.clients,
                    level.maxclients) )
    {
        __debugbreak();
    }
    pEnt = &g_entities[pSelf - level.clients];
    sTeam = (unsigned __int16)Scr_GetConstString(0, SCRIPTINSTANCE_SERVER);
    if ( sTeam == scr_const.none )
    {
        pEnt->s.faction.iHeadIconTeam = 0;
    }
    else if ( sTeam == scr_const.allies )
    {
        pEnt->s.faction.iHeadIconTeam = 2;
    }
    else if ( sTeam == scr_const.axis )
    {
        pEnt->s.faction.iHeadIconTeam = 1;
    }
    else if ( sTeam == scr_const.spectator )
    {
        v1 = SL_ConvertToString(sTeam, SCRIPTINSTANCE_SERVER);
        v2 = va("'%s' is an illegal head icon team string. Must be none, allies, axis, or spectator.", v1);
        Scr_Error(v2, 0);
    }
    else
    {
        pEnt->s.faction.iHeadIconTeam = 3;
    }
}

void __cdecl ClientScr_GetHeadIconTeam(gclient_s *pSelf, const client_fields_s *__formal)
{
    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 699, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    if ( (unsigned int)(pSelf - level.clients) >= level.maxclients
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    700,
                    0,
                    "pSelf - level.clients doesn't index level.maxclients\n\t%i not in [0, %i)",
                    pSelf - level.clients,
                    level.maxclients) )
    {
        __debugbreak();
    }
    //v1.iHeadIconTeam = (unsigned __int8)g_entities[pSelf - level.clients].s.faction.iHeadIconTeam;
    int teamAndOwnerIndex = g_entities[pSelf - level.clients].s.faction.teamAndOwnerIndex;
    switch (teamAndOwnerIndex)
    {
        case 1u:
            Scr_AddConstString(scr_const.axis, SCRIPTINSTANCE_SERVER);
            break;
        case 2u:
            Scr_AddConstString(scr_const.allies, SCRIPTINSTANCE_SERVER);
            break;
        case 3u:
            Scr_AddConstString(scr_const.spectator, SCRIPTINSTANCE_SERVER);
            break;
        default:
            Scr_AddConstString(scr_const.none, SCRIPTINSTANCE_SERVER);
            break;
    }
}

void __cdecl ClientScr_SetArchiveTime(gclient_s *pSelf, const client_fields_s *__formal)
{
    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 726, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    pSelf->sess.archiveTime = (int)(Scr_GetFloat(0, SCRIPTINSTANCE_SERVER) * 1000.0);
}

void __cdecl ClientScr_GetArchiveTime(gclient_s *pSelf, const client_fields_s *__formal)
{
    Scr_AddFloat((float)pSelf->sess.archiveTime * 0.001, SCRIPTINSTANCE_SERVER);
}

void __cdecl ClientScr_SetPSOffsetTime(gclient_s *pSelf, const client_fields_s *__formal)
{
    if ( !pSelf
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 749, 0, "%s", "pSelf") )
    {
        __debugbreak();
    }
    pSelf->sess.psOffsetTime = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
}

void __cdecl ClientScr_GetEntityFromIndex(gclient_s *pSelf, const client_fields_s *pField)
{
    unsigned int entNum; // [esp+4h] [ebp-4h]

    entNum = GetField((const int *)((char *)&pSelf->ps.commandTime + pField->ofs), pField->size[0]);
    if ( entNum > 0x400
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    774,
                    0,
                    "entNum not in [0, MAX_GENTITIES]\n\t%i not in [%i, %i]",
                    entNum,
                    0,
                    1024) )
    {
        __debugbreak();
    }
    if ( entNum != 1023 && entNum != 1022 )
    {
        if ( !g_entities[entNum].r.inuse
            && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 783, 0, "%s", "ent->r.inuse") )
        {
            __debugbreak();
        }
        Scr_AddEntity(&g_entities[entNum], SCRIPTINSTANCE_SERVER);
    }
}

void __cdecl ClientScr_GetPSOffsetTime(gclient_s *pSelf, const client_fields_s *__formal)
{
    Scr_AddInt(pSelf->sess.archiveTime, SCRIPTINSTANCE_SERVER);
}

void __cdecl GScr_AddFieldsForClient()
{
    const client_fields_s *f; // [esp+4h] [ebp-4h]

    for ( f = fields; f->name; ++f )
    {
        // zombies: the SP client rows (downs, revives, ...) on every SP level (Com_IsSPLevel), so the SP front
        // end's _callbackglobal::callback_playerconnect reads self.downs like SP.
        if ( f - fields >= 41 && !Com_IsSPLevel() )
            continue;
        // zombies: maxhealth belongs to gentity in SP (SP 0x00a5576c).
        // KB's class registrar rejects duplicate names; preserve the MP client row otherwise.
        if ( Com_IsSPLevel() && !strcmp(f->name, "maxhealth") )
            continue;
        if ( ((f - fields) & 0xC000) != 0
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                        893,
                        0,
                        "%s",
                        "!((f - fields) & ENTFIELD_MASK)") )
        {
            __debugbreak();
        }
        if ( f - fields != (unsigned __int16)(f - fields)
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                        894,
                        0,
                        "%s",
                        "(f - fields) == (unsigned short)( f - fields )") )
        {
            __debugbreak();
        }
        Scr_AddClassField(0, (char *)f->name, (unsigned __int16)(f - fields) | 0xC000, SCRIPTINSTANCE_SERVER);
    }
}

void __cdecl Scr_SetClientField(gclient_s *client, int offset)
{
    const client_fields_s *f; // [esp+0h] [ebp-4h]

    if ( !client
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 909, 0, "%s", "client") )
    {
        __debugbreak();
    }
    if ( (unsigned int)offset >= ARRAY_COUNT(fields) - 1
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    910,
                    0,
                    "%s",
                    "static_cast<unsigned int>( offset ) < ARRAY_COUNT( fields ) - 1") )
    {
        __debugbreak();
    }
    if ( offset < 0
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 911, 0, "%s", "offset >= 0") )
    {
        __debugbreak();
    }
    f = &fields[offset];
    if ( f->setter )
    {
        f->setter(client, f);
    }
    else
    {
        if ( !f->ofs
            && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 921, 0, "%s", "f->ofs") )
        {
            __debugbreak();
        }
        // Forward the row mask, as Scr_SetScalarField does (SP 0x00656117).
        GScr_SetGenericField((unsigned __int8 *)client, f->type, f->ofs, f->whichbits);
    }
}

void __cdecl Scr_GetClientField(gclient_s *client, int offset)
{
    const client_fields_s *f; // [esp+0h] [ebp-4h]

    if ( !client
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 940, 0, "%s", "client") )
    {
        __debugbreak();
    }
    if ( (unsigned int)offset >= ARRAY_COUNT(fields) - 1
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp",
                    941,
                    0,
                    "%s",
                    "static_cast<unsigned int>( offset ) < ARRAY_COUNT( fields ) - 1") )
    {
        __debugbreak();
    }
    if ( offset < 0
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 942, 0, "%s", "offset >= 0") )
    {
        __debugbreak();
    }
    f = &fields[offset];
    if ( f->getter )
    {
        f->getter(client, f);
    }
    else
    {
        if ( !f->ofs
            && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\game\\g_client_fields.cpp", 952, 0, "%s", "f->ofs") )
        {
            __debugbreak();
        }
        GScr_GetGenericField((unsigned __int8 *)client, f->type, f->ofs, f->whichbits);
    }
}

