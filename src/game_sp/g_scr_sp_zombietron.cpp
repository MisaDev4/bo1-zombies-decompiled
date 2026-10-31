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
// SP writes the text into configstring 0x142 + index for its SP objective display, which the port does not
// have (KB's MP objectives are a different system and the zombietron HUD is not ported, cg_sp_hud.cpp).
static void GScrSP_Objective_String()
{
    GScrSP_GetObjectiveIndex();
}

// zombies: Objective_Set3D( index, enable, [colour], [text], ... ) (SP 0x007f7530). NOT PORTED beyond the index
// check: SP sets the 3D marker bits of its level objective (SP 0x01c0418c + index * 0x30, +0x24 / +0x28) and its
// text configstring 0x142 + index; DOA's arena exit arrows and tutorial prompts. See Objective_String.
static void GScrSP_Objective_Set3D()
{
    GScrSP_GetObjectiveIndex();
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
// no such field, so it is kept here (cleared with the client's fields) and not sent. SP indexes the client state by entnum
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
    if (GScrSP_Player(entref))
        s_scoreMultiplier[entref.entnum] = multiplier;
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
