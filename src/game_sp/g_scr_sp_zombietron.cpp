// SP builtin table, lane Z: the builtins Dead Ops Arcade's (map zombietron) scripts need that no other table
// has. Ported from the SP exe (read-only copy, disassembled). Searched after BO1Zombies's own tables and before
// the generated not-ported table (see src/game_sp/scr_sp_tables.h).
#include "scr_sp_tables.h"
#include "g_scr_sp_zombietron.h"
#include <game_mp/g_main_mp.h>
#include <game_mp/g_spawn_mp.h>
#include <game/g_weapon.h>
#include <game/bullet.h>
#include <game_mp/g_utils_mp.h>
#include <bgame/bg_weapons.h>
#include <bgame/bg_weapons_def.h>
#include <bgame/bg_weapons_load_obj.h>
#include <bgame/bg_misc.h>
#include <universal/com_math.h>
#include <universal/com_math_anglevectors.h>
#include <game/actor_corpse.h>
#include <bgame/bg_local.h>
#include <clientscript/cscr_vm.h>
#include <universal/dvar.h>
#include <cgame_mp/cg_scoreboard_mp.h>
#include <game_mp/g_scr_main_mp.h>
#include <game_mp/g_utils_mp.h>
#include <cstring>

// zombies: GetDvarVector, the server one (SP 0x007f0380). Same as CScr_GetDvarVector (cg_scr_main.cpp) on the
// server instance, with the server's shorter "not found" text.
static void GScrSP_GetDvarVector()
{
    const dvar_s *dvar;
    if (Scr_GetType(0, SCRIPTINSTANCE_SERVER) == 6)
        dvar = Dvar_FindVar(Scr_GetInt(0, SCRIPTINSTANCE_SERVER));
    else
        dvar = Dvar_FindVar(Scr_GetString(0, SCRIPTINSTANCE_SERVER));
    if (!dvar)
    {
        Scr_Error("Invalid Dvar passed into GetDvarVec", false);
        return;
    }
    if (dvar->type != DVAR_TYPE_FLOAT_3)
        Scr_Error("dvar is not a vector in but GetDvarVec has been called on it", false);
    Scr_AddVector((float *)&dvar->current.value, SCRIPTINSTANCE_SERVER);
}

// zombies: ClearAllCorpses (SP 0x008003d0, a jump to SP 0x005953a0 = G_RemoveAllActorCorpses).
static void GScrSP_ClearAllCorpses()
{
    G_RemoveAllActorCorpses();
}

// zombies: SetWeapon( ent, weaponName ) (SP 0x0052cd10): sets the entity's s.weapon (SP gentity+0xd4, the
// entityState "weapon" netfield). DOA's double-shot orbs take the player's weapon with it, then Fire() it.
static void GScrSP_SetWeapon()
{
    gentity_s *ent = Scr_GetEntity(0);
    char *weaponName = Scr_GetString(1, SCRIPTINSTANCE_SERVER);
    int weapon = G_GetWeaponIndexForName(weaponName);
    if (!weapon)
        Scr_Error(va("SetWeapon called with unknown weapon name %s\n", weaponName), false);
    // SP stores a byte (its s.weapon is 8 bits); KB's is a u16 holding the same index.
    ent->s.weapon = (unsigned __int16)weapon;
}

// zombies: Fire( ent, owner ) (SP 0x00472990): fires ent's s.weapon from its origin along its angles, on behalf
// of owner, then adds EV_FIRE_WEAPON to ent. Right / up stay zero and the spread 0 except for WEAPCLASS_SPREAD,
// which spreads by fHipSpreadStandMin (SP weapDef+0x4cc) around SP's MakeNormalVectors (SP 0x004c64e0).
static void GScrSP_Fire()
{
    gentity_s *ent = Scr_GetEntity(0);
    gentity_s *owner = Scr_GetEntity(1);
    const unsigned int weapon = ent->s.weapon & 0xFF; // SP reads its byte s.weapon
    weaponParms wp;
    wp.weapVariantDef = BG_GetWeaponVariantDef(weapon);
    wp.weapDef = BG_GetWeaponDef(weapon);
    AngleVectors(ent->r.currentAngles, wp.forward, nullptr, nullptr);
    Vec3Copy(ent->r.currentOrigin, wp.muzzleTrace);
    Vec3Copy(wp.forward, wp.gunForward); // SP leaves it unset; KB's fire paths may read it
    Vec3Clear(wp.right);
    Vec3Clear(wp.up);
    const float targetOffset[3] = { 0.0f, 0.0f, 0.0f };
    float spread = 0.0f;
    const WeaponDef *weapDef = wp.weapDef;
    if (weapDef->weapType == WEAPTYPE_GRENADE || weapDef->weapType == WEAPTYPE_MINE)
        Scr_Error("Fire() does not work with grenade-type weapons.\n", false);
    if (weapDef->weapClass == WEAPCLASS_SPREAD)
    {
        spread = weapDef->fHipSpreadStandMin;
        PerpendicularVector(wp.forward, wp.right);
        Vec3Cross(wp.forward, wp.right, wp.up);
    }
    switch (weapDef->weapType)
    {
    case WEAPTYPE_BULLET:
        Bullet_Fire(owner, spread, &wp, ent, level.time);
        break;
    case WEAPTYPE_PROJECTILE:
        if (weapDef->weapClass == WEAPCLASS_GRENADE)
            Weapon_GrenadeLauncher_Fire(owner, weapon, 0, &wp);
        else if (weapDef->weapClass == WEAPCLASS_ROCKETLAUNCHER || weapDef->weapClass == WEAPCLASS_PISTOL
            || weapDef->weapClass == WEAPCLASS_TURRET)
            Weapon_RocketLauncher_Fire(owner, weapon, 0.0f, &wp, vec3_origin, nullptr, targetOffset);
        else
            Scr_Error("Fire(): Unhandled projectile weapClass.\n", false);
        break;
    case WEAPTYPE_GAS:
        Weapon_Flamethrower_Fire(ent, &wp);
        break;
    default:
        Scr_Error(va("Fire(): Unhandled weapType \"%s\".\n", BG_GetWeaponTypeName(weapDef->weapType)), false);
        break;
    }
    G_AddEvent(ent, EV_FIRE_WEAPON, 0);
}

// zombies: SP's objective index check (SP 0x007f6e80 / 0x007f7530: 32 objectives).
static int GScrSP_GetObjectiveIndex()
{
    int index = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
    if (index < 0 || index >= 32)
        Scr_Error(va("index %i is an illegal objective index. Valid indexes are 0 to %i\n", index, 31), false);
    return index;
}

// zombies: Objective_String( index, text ) (SP 0x007f70b0 -> 0x007f6e80). NOT PORTED beyond the index check:
// SP writes the text into configstring 0x142 + index for its objectives list (the pause menu's), which the
// port does not have. The 3D marker's text is Objective_Set3D's.
static void GScrSP_Objective_String()
{
    GScrSP_GetObjectiveIndex();
}

// SP keeps the 3D marker's text in its objective configstring ("txt3d"). The port sends it with the objective
// instead, as a CS_LOCALIZED_STRINGS index in objective_t's alt_3D_text slot (SP objective +0x2c; KB never uses
// that pointer), networked by the zombiemode objective fields (MSG_GetNetFieldList). 0 = no text.
void G_SP_SetObjective3DText(objective_t *obj, int localizedStringIndex)
{
    static_assert(sizeof(obj->alt_3D_text) == sizeof(int), "alt_3D_text carries an int");
    memcpy(&obj->alt_3D_text, &localizedStringIndex, sizeof(int));
}

int G_SP_GetObjective3DText(const objective_t *obj)
{
    int localizedStringIndex;
    memcpy(&localizedStringIndex, &obj->alt_3D_text, sizeof(int));
    return localizedStringIndex;
}

// zombies: Objective_Set3D( index, enable, [colour], [text], [ui3dWindow], [offset] ) (SP 0x007f7530): the
// objective's 3D marker (ownerdraw 322, cg_sp_hud.cpp). use3D (SP +0x24) holds enable in bit 0 and the UI3D
// window + 1 in bits 1..4; color (SP +0x28) is 1 white, 2 red, 3 green, 4 blue, 5 ltblue, 0 any other name
// (cg_objectiveIndicatorColor). The text "*" draws no on-screen marker, only the off-screen arrow (DOA's exits);
// no text shows the distance. NOT PORTED: the offset (SP keys offx / offy / offz), which only the 6-argument form
// sets and DOA does not use.
static void GScrSP_Objective_Set3D()
{
    const int numParam = Scr_GetNumParam(SCRIPTINSTANCE_SERVER);
    objective_t *obj = &level.objectives[GScrSP_GetObjectiveIndex()];
    char text[1024];

    obj->use3D = 0;
    obj->color = 0;
    text[0] = 0;
    switch (numParam)
    {
    case 6:
    case 5:
    {
        const int window = Scr_GetInt(4, SCRIPTINSTANCE_SERVER);
        obj->use3D |= ((window < 0 ? 0 : window + 1) & 0xF) * 2;
    }
        [[fallthrough]];
    case 4:
        Scr_ConstructMessageString(3, 3, "Objective String", text, sizeof(text));
        if (strlen(text) >= sizeof(text) - 1)
            Scr_Error(va("Objective strings is too long (> %i): %s\n", 1023, text), false);
        [[fallthrough]];
    case 3:
    {
        const char *colorName = Scr_GetString(2, SCRIPTINSTANCE_SERVER);
        if (!strcmp(colorName, "white"))
            obj->color = 1;
        else if (!strcmp(colorName, "red"))
            obj->color = 2;
        else if (!strcmp(colorName, "green"))
            obj->color = 3;
        else if (!strcmp(colorName, "blue"))
            obj->color = 4;
        else if (!strcmp(colorName, "ltblue"))
            obj->color = 5;
    }
        [[fallthrough]];
    case 2:
        obj->use3D |= Scr_GetInt(1, SCRIPTINSTANCE_SERVER) & 1;
        break;
    default:
        Scr_Error(va("Invalid argument count. Expected 2-6, given %d\n", numParam), false);
        break;
    }
    G_SP_SetObjective3DText(obj, text[0] ? G_LocalizedStringIndex(text) : 0);
}

// zombies: SetScale( scale ) (SP 0x007f4bc0), script movers only. Stores scale * 6553.5 + 0.5 (scale 0..10
// in 16 bits) in s.lerp.u.scriptMover.scale (SP es+0x6e, netfield "lerp.u.scriptMover.scale", 16 bits).
// NOT PORTED: KB's entity netfields do not send that field and its cgame draws no model scale, so DOA's
// pickups draw at their normal size.
static void GScrSP_SetScale(scr_entref_t entref)
{
    gentity_s *ent = nullptr;
    if (entref.classnum)
        Scr_ObjectError("not an entity", SCRIPTINSTANCE_SERVER);
    else
        ent = &g_entities[entref.entnum];
    float scale = Scr_GetFloat(0, SCRIPTINSTANCE_SERVER);
    if (!ent)
        return; // SP goes on with a null entity
    if (ent->s.eType != ET_SCRIPTMOVER)
        Scr_Error("Can only set scale on script movers", false);
    ent->s.lerp.u.scriptMover.scale = (unsigned __int16)(int)(scale * 6553.5f + 0.5f);
}

static gentity_s *GScrSP_Player(scr_entref_t entref)
{
    if (entref.classnum)
    {
        Scr_ObjectError("not an entity", SCRIPTINSTANCE_SERVER);
        return nullptr;
    }
    gentity_s *ent = &g_entities[entref.entnum];
    if (!ent->client)
    {
        Scr_ObjectError(va("entity %i is not a player", entref.entnum), SCRIPTINSTANCE_SERVER);
        return nullptr;
    }
    return ent;
}

// zombies: ClearDamageIndicator() (SP 0x007d9a90): client damage_blood (SP gclient+0x1c3c) = 0. SP writes
// through the null client after its "not a player" error; the port returns.
static void PlayerCmdSP_ClearDamageIndicator(scr_entref_t entref)
{
    if (gentity_s *ent = GScrSP_Player(entref))
        ent->client->damage_blood = 0;
}

// zombies: GetScoreMultiplier() / SetScoreMultiplier( n ) (SP 0x007d9bc0 / 0x007d9be0): clientState
// score.scoreMultiplier (SP cs+0xc8, a 14 bit netfield), which only the zombietron HUD reads. KB's score_s has
// no such field, so it is kept here (cleared with the client's fields) and sent as a scoreboard column. SP indexes the client state by entnum
// without a player check; the port checks.
static int s_scoreMultiplier[ARRAY_COUNT(g_clients)];

void G_SP_ClearZombietronClientState(unsigned int clientNum)
{
    iassert(clientNum < ARRAY_COUNT(s_scoreMultiplier));
    s_scoreMultiplier[clientNum] = 0;
}

static void PlayerCmdSP_GetScoreMultiplier(scr_entref_t entref)
{
    if (GScrSP_Player(entref))
        Scr_AddInt(s_scoreMultiplier[entref.entnum], SCRIPTINSTANCE_SERVER);
}

static void PlayerCmdSP_SetScoreMultiplier(scr_entref_t entref)
{
    int multiplier = Scr_GetInt(0, SCRIPTINSTANCE_SERVER);
    if (gentity_s *ent = GScrSP_Player(entref))
    {
        s_scoreMultiplier[entref.entnum] = multiplier;
        G_SP_SetScoreColumn(ent->client, SB_ZT_MULTIPLIER, multiplier); // the HUD's copy (g_scr_sp_zombietron.h)
    }
}

const BuiltinFunctionDef g_sp_zombietron_functions[] =
{
    { "getdvarvector", GScrSP_GetDvarVector, 0 },     // SP row .data 0x00b76240
    { "clearallcorpses", GScrSP_ClearAllCorpses, 0 }, // SP row 0x00b76f30
    { "setweapon", GScrSP_SetWeapon, 0 },             // SP row 0x00b76d20
    { "fire", GScrSP_Fire, 0 },                       // SP row 0x00b76d2c
    { "objective_string", GScrSP_Objective_String, 0 }, // SP row 0x00b76444
    { "objective_set3d", GScrSP_Objective_Set3D, 0 },   // SP row 0x00b764c8
    { nullptr, nullptr, 0 } // end marker, keep last
};
const unsigned int g_sp_zombietron_function_count = ARRAY_COUNT(g_sp_zombietron_functions) - 1;

const BuiltinMethodDef g_sp_zombietron_methods[] =
{
    { "setscale", GScrSP_SetScale, 0 },                               // SP row .rdata 0x00a54530
    { "cleardamageindicator", PlayerCmdSP_ClearDamageIndicator, 0 },  // SP row 0x00a53134
    { "getscoremultiplier", PlayerCmdSP_GetScoreMultiplier, 0 },      // SP row 0x00a531b8
    { "setscoremultiplier", PlayerCmdSP_SetScoreMultiplier, 0 },      // SP row 0x00a531c4
    { nullptr, nullptr, 0 } // end marker, keep last
};
const unsigned int g_sp_zombietron_method_count = ARRAY_COUNT(g_sp_zombietron_methods) - 1;
