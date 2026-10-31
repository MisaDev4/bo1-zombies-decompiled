// SP builtin table, lane F: client (CSC) builtins.
// Searched after BO1Zombies's own tables and before the generated not-ported table (see
// src/game_sp/scr_sp_tables.h).
#include <gfx_d3d/r_scene.h>
#include <ragdoll/ragdoll.h>
#include <bgame/bg_weapons_def.h>
#include <cgame/cg_weapons.h>
#include <bgame/bg_misc.h>
#include <clientscript/scr_const.h>
#include <game_sp/scr_sp_tables.h>
#include <game_sp/g_scr_sp_entity.h>
#include <clientscript/cscr_vm.h>
#include <cgame_mp/cg_local_mp.h>
#include <cgame_mp/cg_ents_mp.h>
#include <cgame/cg_spawn.h>
#include <bgame/bg_weapons_def.h>
#include <gfx_d3d/r_shader_constant_set.h>
#include <xanim/dobj_utils.h>
#include <EffectsCore/fx_marks.h>
#include <DynEntity/DynEntity_client.h>
#include <DynEntity/DynEntity_load_obj.h>
#include <clientscript/cscr_stringlist.h>
#include <stringed/stringed_hooks.h>
#include <universal/com_math_anglevectors.h>
#include <emmintrin.h>
#include <qcommon/cmd.h>
#include <qcommon/com_clients.h>
#include <client_mp/cl_input_mp.h>
#include <win32/win_gamerprofile.h>
#include <cgame_mp/cg_main_mp.h>
#include <bgame/bg_mantle.h>
#include <bgame/bg_weapons.h>
#include <bgame/bg_pmove.h>
#include <cgame_mp/cg_predict_mp.h>
#include <cgame/cg_scr_main.h>
#include <qcommon/cm_trace.h>
#include <qcommon/dobj_management.h>
#include <cmath>
#include <gfx_d3d/r_fog.h>
#include <client/splitscreen.h>
#include <cgame/cg_scr_sp_blur.h>
#include <cgame/cg_sp_client_ext.h>
#include <qcommon/common.h>
#include <universal/dvar.h>

// zombies: haseyes (SP 0x00896970).
static void __cdecl CScr_HasEyes(scr_entref_t entref)
{
    centity_s *cent = CG_GetEntity(entref.client, entref.entnum);
    // KB remaps SP's eFlags bit 17 to eFlags2 bit 31 to preserve destructibles.
    Scr_AddBool((cent->currentState.eFlags2 & SP_EFLAGS2_HAS_EYES) != 0, SCRIPTINSTANCE_CLIENT);
}

// zombies: mapshaderconstant (SP 0x008961a0).
static void __cdecl CScr_MapShaderConstant(scr_entref_t entref)
{
    float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (entref.classnum)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "not an entity", false);

    unsigned int numParam = Scr_GetNumParam(SCRIPTINSTANCE_CLIENT);
    if (numParam < 3 || numParam > 7)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "USAGE: ent mapshaderconstant( <localClientNum>, <index>, <constant name>)\n", false);

    int localClientNum = CScr_GetLocalClientNum(0);
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    unsigned int index = Scr_GetInt(1, SCRIPTINSTANCE_CLIENT);
    const char *name = Scr_GetString(2, SCRIPTINSTANCE_CLIENT);
    for (unsigned int i = 3; i < numParam; ++i)
        value[i - 3] = (float)Scr_GetFloat(i, SCRIPTINSTANCE_CLIENT);

    centity_s *cent = CG_GetEntity(localClientNum, entref.entnum);
    if (cent && R_MapShaderConstantSet(&cent->pose.constantSet, index, name))
    {
        R_SetShaderConstantSetValue(&cent->pose.constantSet, index, value);
        Scr_AddBool(true, SCRIPTINSTANCE_CLIENT);
        // SP reads cg+0x34, the NEXT snapshot (its CG_IsLocalPlayer 0x00667ef0 reads the same
        // pointer, and KB's CG_IsLocalPlayer names it nextSnap); lane F had it as snap.
        if (!cgameGlob->nextSnap)
            return;
        if (cgameGlob->nextSnap->ps.clientNum != entref.entnum)
            return;
        if (!R_MapShaderConstantSet(&cgameGlob->viewModelPose.constantSet, index, name))
            return;
        R_SetShaderConstantSetValue(&cgameGlob->viewModelPose.constantSet, index, value);
        return;
    }
    Scr_AddBool(false, SCRIPTINSTANCE_CLIENT);
}

// zombies: triggerfx (SP 0x00894140).
static void __cdecl CScr_TriggerFX()
{
    unsigned int numParam = Scr_GetNumParam(SCRIPTINSTANCE_CLIENT);
    if (!numParam || numParam >= 3)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "Incorrect number of parameters", false);

    scr_entref_t entref = Scr_GetEntityRef(0, SCRIPTINSTANCE_CLIENT);
    centity_s *cent = CG_GetEntity(entref.client, entref.entnum);
    if (cent->nextState.eType != ET_FX)
        Scr_ParamError(0, "entity wasn't created with 'newFx'", SCRIPTINSTANCE_CLIENT);

    cg_s *cgameGlob = CG_GetLocalClientGlobals(entref.client);
    // zombies: opt-in diagnostics for client spawnfx/triggerfx entities; not an SP behavior change.
    if (CG_SP_VisualsEnabled())
    {
        const FxEffectDef *fxDef = CG_GetLocalClientStaticGlobals(entref.client)->fxs[cent->nextState.un1.scale];
        Com_Printf(14, "BO1_VISUAL trigger_fx time=%d client=%d ent=%d fx=%s origin=%.1f,%.1f,%.1f\n",
            cgameGlob->time, entref.client, entref.entnum, fxDef ? fxDef->name : "?",
            cent->pose.origin[0], cent->pose.origin[1], cent->pose.origin[2]);
        CG_SP_VisualShot(entref.client, fxDef ? fxDef->name : 0);
    }
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) == 2)
    {
        // SP stores the product as float, adds the double epsilon, then uses FISTP.
        float time = (float)(Scr_GetFloat(1, SCRIPTINSTANCE_CLIENT) * 1000.0f);
        cent->nextState.time2 = _mm_cvtsd_si32(_mm_set_sd((double)time + 9.313225746154785e-10));
        return;
    }
    cent->nextState.time2 = cgameGlob->time;
}

// zombies: useweaponhidetags (SP 0x008974d0).
static void __cdecl CScr_UseWeaponHideTags(scr_entref_t entref)
{
    if (!Scr_GetNumParam(SCRIPTINSTANCE_CLIENT))
        Scr_Error(SCRIPTINSTANCE_CLIENT, "useweaponhidetags <weaponName>.\n", false);
    const char *name = Scr_GetString(0, SCRIPTINSTANCE_CLIENT);
    unsigned int weaponIndex = BG_GetWeaponIndexForName(name);
    if (!weaponIndex)
        Scr_Error(SCRIPTINSTANCE_CLIENT, va("useweaponhidetags called with unknown weapon name %s\n", name), false);

    const WeaponVariantDef *weapDef = BG_GetWeaponVariantDef(weaponIndex);
    centity_s *cent = CG_GetEntity(entref.client, entref.entnum);
    memset(cent->nextState.partBits, 0, sizeof(cent->nextState.partBits));
    cgs_t *cgs = CG_GetLocalClientStaticGlobals(entref.client);
    DObj *obj = CG_PreProcess_GetDObj(entref.client, cent->nextState.number, cent->nextState.eType,
        cgs->gameModels[cent->nextState.index.brushmodel], 0);
    if (obj)
    {
        for (unsigned int i = 0; i < 32 && weapDef->hideTags[i]; ++i)
        {
            unsigned char boneIndex = 0xFE;
            if (DObjGetBoneIndex(obj, weapDef->hideTags[i], &boneIndex, -1))
                cent->nextState.partBits[boneIndex >> 5] |= 0x80000000u >> (boneIndex & 31);
        }
        unsigned int oldPartBits[5];
        DObjGetHidePartBits(obj, oldPartBits);
        DObjSetHidePartBits(obj, cent->nextState.partBits);
        FX_MarkEntUpdateHidePartBits(oldPartBits, cent->nextState.partBits, cent->pose.localClientNum, cent->nextState.number);
    }
}

// zombies: the dynent lookup behind getdynent (SP 0x00618B10): the first model (drawType 0), then brush (1), dynent
// whose targetname matches; returns the per-drawType index (not the abs id, as SP), or 0xFFFF.
static unsigned __int16 __cdecl DynEnt_GetIdByTargetname(unsigned int targetname)
{
    for (int drawType = 0; drawType < 2; ++drawType)
    {
        unsigned __int16 count = DynEnt_GetEntityCount((DynEntityCollType)drawType);
        for (unsigned __int16 id = 0; id < count; ++id)
        {
            if (DynEnt_GetEntityDef(id, (DynEntityDrawType)drawType)->targetname == targetname)
                return id;
        }
    }
    return 0xFFFF;
}

// zombies: getdynent( <targetname> ) (SP C_f_getdynent 0x00895D00): a dynentity (class 4) ref, or a script error.
// WaW maps use it (zombie_cod5_sumpf.csc swamp_german_safe, zombie_cod5_asylum.csc morgue_lamp).
static void __cdecl CScr_GetDynEnt()
{
    unsigned int targetname = CScr_GetConstServerString(0);
    unsigned __int16 id = DynEnt_GetIdByTargetname(targetname);
    if (id == 0xFFFF)
    {
        Scr_Error(SCRIPTINSTANCE_CLIENT, va("getdynent, failed to find dyn ent with targetname %s.", SL_ConvertToString(targetname, SCRIPTINSTANCE_SERVER)), false);
        return;
    }
    Scr_AddEntityNum(id, 4, SCRIPTINSTANCE_CLIENT, 0);
}

// zombies: createdynentandlaunch (SP 0x00557820).
static void __cdecl CScr_CreateDynEntAndLaunch()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) < 6)
        Scr_Error(SCRIPTINSTANCE_CLIENT,
            "CreateDynEntAndLaunch called with invalid params. CreateDynEntAndLaunch( <local client num>, <model>, <pos>, <angles>, <hitpos>, <force>, <fx>, <mature> )", false);

    int fxId = 0;
    int localClientNum = Scr_GetInt(0, SCRIPTINSTANCE_CLIENT);
    const char *name = Scr_GetString(1, SCRIPTINSTANCE_CLIENT);
    int modelIndex = CG_GetModelIndex(name, localClientNum);
    if (modelIndex < 0)
    {
        Scr_Error(SCRIPTINSTANCE_CLIENT, va("model '%s' not precached\n", name), true);
        return;
    }
    float pos[3], angles[3], hitpos[3], force[3], quat[4];
    Scr_GetVector(2, pos, SCRIPTINSTANCE_CLIENT);
    Scr_GetVector(3, angles, SCRIPTINSTANCE_CLIENT);
    Scr_GetVector(4, hitpos, SCRIPTINSTANCE_CLIENT);
    Scr_GetVector(5, force, SCRIPTINSTANCE_CLIENT);
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) > 6)
        fxId = Scr_GetInt(6, SCRIPTINSTANCE_CLIENT);
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) > 7 && Scr_GetInt(7, SCRIPTINSTANCE_CLIENT))
    {
        int language = loc_language->current.integer;
        if (language == 3)
            return;
        // SP DAT_0243fdd4 is zombiemode (also used by GScr_LoadScripts 0x00580370).
        if ((language == 11 || language == 13) && !zombiemode->current.enabled)
            return;
    }
    cgs_t *cgs = CG_GetLocalClientStaticGlobals(localClientNum);
    XModel *model = cgs->gameModels[modelIndex];
    AnglesToQuat(angles, quat);
    unsigned short id = DynEntCl_CreateEntityModel(model, quat, pos, hitpos, force, 0, 0);
    if (fxId)
        DynEntCl_PlayBoltedFX(cgs->fxs[fxId], id);
}

// zombies: forcegamemodemappings (SP 0x00498c80).
static void __cdecl CScr_ForceGameModeMappings()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 2)
    {
        Scr_Error(SCRIPTINSTANCE_CLIENT,
            "Incorrect number of parameters passed to ForceGameModeMappings()\nUSAGE: ForceGameModeMappings( <localClientNum>, <modeName>)\n", false);
        return;
    }
    int localClientNum = Scr_GetInt(0, SCRIPTINSTANCE_CLIENT);
    const char *mode = Scr_GetString(1, SCRIPTINSTANCE_CLIENT);
    if (!I_stricmp(mode, "default"))
    {
        if (Dvar_GetBool("gpad_enabled"))
        {
            // SP 0x0066a9b0 is GamerProfile_ExecControllerBindings, not a game-mode setter.
            GamerProfile_ExecControllerBindings(Com_LocalClient_GetControllerIndex(localClientNum));
            CL_ClearKeys(localClientNum);
            return;
        }
    }
    else if (!I_stricmp(mode, "zombietron"))
    {
        if (Dvar_GetBool("gpad_enabled"))
        {
            int controllerIndex = Com_LocalClient_GetControllerIndex(localClientNum);
            Cmd_ExecuteSingleCommand(localClientNum, controllerIndex, (char *)"exec thumbstick_default.cfg\n");
            Cmd_ExecuteSingleCommand(localClientNum, controllerIndex, (char *)"exec buttons_default.cfg\n");
            Cmd_ExecuteSingleCommand(localClientNum, controllerIndex, (char *)"exec buttons_zombietron\n");
        }
    }
    // SP 0x005d9410 clears 42 kbutton_t entries (0x3f0 bytes); KB owns 47.
    CL_ClearKeys(localClientNum);
}

// zombies: getlocalclienthealth (SP 0x00894d80): the predicted playerState's STAT_HEALTH (ps+0x1c4).
static void __cdecl CScr_GetLocalClientHealth()
{
    int localClientNum = CScr_GetLocalClientNum(0);
    Scr_AddInt(CG_GetPredictedPlayerState(localClientNum)->stats[0], SCRIPTINSTANCE_CLIENT);
}

// zombies: getlocalclientmaxhealth (SP 0x00894dd0): STAT_MAX_HEALTH (ps+0x1cc).
static void __cdecl CScr_GetLocalClientMaxHealth()
{
    int localClientNum = CScr_GetLocalClientNum(0);
    Scr_AddInt(CG_GetPredictedPlayerState(localClientNum)->stats[2], SCRIPTINSTANCE_CLIENT);
}

// zombies: getweaponammoclip (SP 0x00894e20): the rounds in the named weapon's clip (the ammoInClip
// slot whose clipIndex is the variant's iClipIndex), 0 when the player has none.
static void __cdecl CScr_GetWeaponAmmoClip()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 2)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "GetWeaponAmmoClip() called with wrong params.\n", false);
    int localClientNum = CScr_GetLocalClientNum(0);
    unsigned int weaponIndex = BG_FindWeaponIndexForName(Scr_GetString(1, SCRIPTINSTANCE_CLIENT));
    const playerState_s *ps = CG_GetPredictedPlayerState(localClientNum);
    const WeaponVariantDef *weapVarDef = BG_GetWeaponVariantDef(weaponIndex);
    Scr_AddInt(BG_GetAmmoInClipForWeaponDef(ps, weapVarDef), SCRIPTINSTANCE_CLIENT);
}

// zombies: isads (SP 0x00652fd0): fWeaponPosFrac above 0.
static void __cdecl CScr_IsADS()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 1)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "IsADS() called with wrong params.\n", false);
    int localClientNum = CScr_GetLocalClientNum(0);
    Scr_AddInt(CG_GetPredictedPlayerState(localClientNum)->fWeaponPosFrac > 0.0f, SCRIPTINSTANCE_CLIENT);
}

// zombies: isthrowinggrenade (SP 0x00894ed0). SP tests weaponstate 21..26, its six offhand states;
// SP's weaponstate enum runs one ahead of KB's from the reload states on (SP MELEE_INIT is 18,
// KB's 0x11), so the same six states are KB's WEAPON_OFFHAND_INIT..WEAPON_OFFHAND_END.
static void __cdecl CScr_IsThrowingGrenade()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 1)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "IsThrowingGrenade() called with wrong params.\n", false);
    int localClientNum = CScr_GetLocalClientNum(0);
    int weaponstate = CG_GetPredictedPlayerState(localClientNum)->weaponstate;
    Scr_AddInt(weaponstate >= WEAPON_OFFHAND_INIT && weaponstate <= WEAPON_OFFHAND_END, SCRIPTINSTANCE_CLIENT);
}

// zombies: ismeleeing (SP 0x00894f60). SP's weaponstates 18, 19, 20 are KB's MELEE_INIT, MELEE_FIRE,
// MELEE_END (see isthrowinggrenade).
static void __cdecl CScr_IsMeleeing()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 1)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "IsMeleeing() called with wrong params.\n", false);
    int localClientNum = CScr_GetLocalClientNum(0);
    int weaponstate = CG_GetPredictedPlayerState(localClientNum)->weaponstate;
    Scr_AddInt(weaponstate == WEAPON_MELEE_INIT || weaponstate == WEAPON_MELEE_FIRE || weaponstate == WEAPON_MELEE_END,
        SCRIPTINSTANCE_CLIENT);
}

// zombies: isonturret (SP 0x008950a0): the predicted eFlags' turret bits (0x300, the same in KB).
static void __cdecl CScr_IsOnTurret()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 1)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "isonturret() called with wrong params.\n", false);
    int localClientNum = CScr_GetLocalClientNum(0);
    Scr_AddInt((CG_GetPredictedPlayerState(localClientNum)->eFlags & 0x300) != 0, SCRIPTINSTANCE_CLIENT);
}

// zombies: the entity a client method's entref names, the way SP's tag / spectator methods pick it
// (inlined in SP 0x00416680 and 0x00897410): the local client's own player is the predicted
// player entity, anything else is its centity. A non-entity entref is a script error.
static centity_s *CScr_SP_GetMethodEntity(scr_entref_t entref)
{
    if (entref.classnum)
    {
        Scr_Error(SCRIPTINSTANCE_CLIENT, "not an entity", false);
        return nullptr;
    }
    if (CG_GetClientNumForLocalClient(entref.client) == entref.entnum)
        return &CG_GetLocalClientGlobals(entref.client)->predictedPlayerEntity;
    return CG_GetEntity(entref.client, entref.entnum);
}

// zombies: EntGetsWeaponFireNotification( <bool> ) (SP C_m_entgetsweaponfirenotification 0x00897360): sets or clears
// SP cent+0x310 bit 0x800000 (KB bEntGetsWeaponFireNotification) that CG_EntityEvent reads on EV_FIRE_WEAPON*.
// SP picks another struct for entnum >= 0x400; KB has no such client entities, so the method entity is used.
static void __cdecl CScr_EntGetsWeaponFireNotification(scr_entref_t entref)
{
    centity_s *cent = CScr_SP_GetMethodEntity(entref);
    unsigned int on = Scr_GetInt(0, SCRIPTINSTANCE_CLIENT) == 1;
    if (!cent)
        return;
    cent->bEntGetsWeaponFireNotification = on;
}

// zombies: EntYawOverridesLinkYaw( <bool> ) (SP C_m_entyawoverrideslinkyaw 0x008972b0): SP cent+0x310 bit 0x400000,
// read by CG_LinkTransformForEntity (SP 0x0050127f).
static void __cdecl CScr_EntYawOverridesLinkYaw(scr_entref_t entref)
{
    centity_s *cent = CScr_SP_GetMethodEntity(entref);
    int on = Scr_GetInt(0, SCRIPTINSTANCE_CLIENT) == 1;
    if (!cent)
        return;
    cent->bEntYawOverridesLinkYaw = on;
}

// zombies: the client physics-gravity table (SP cg+0xbc78, 32 x {centity, gravity}; cleared with cg at SP 0x00490c4a).
// Its reader is CG_SP_ApplyPhysicsGravity (SP 0x0046adb1). The value is an int: SP C_m_setphysicsgravity reads it with
// Scr_GetInt (SP 0x004c1bb0, stored raw at 0x0065082a). Kept outside cg_s: KB's cg_s is exactly the SP-sized 0x71C80 hunk block.
struct CgSpPhysicsGravity
{
    centity_s *cent;
    int gravity;
};
static CgSpPhysicsGravity s_cgSpPhysicsGravity[32];

void CG_SP_ClearPhysicsGravityTable()
{
    memset(s_cgSpPhysicsGravity, 0, sizeof(s_cgSpPhysicsGravity));
}

// zombies: the table's reader in CG_CreateRagdollObject (SP 0x0046ad10, from 0x0046ada4, zombiemode only): find the
// centity's slot; for a created ragdoll (handle > 0) the body takes the override (SP body+0xa24 = found, +0xa28 = value);
// a found slot is freed even when no ragdoll was created (SP 0x0046ae0f).
void CG_SP_ApplyPhysicsGravity(centity_s *cent, int ragdollHandle)
{
    int i;

    for (i = 0; i < 32; ++i)
    {
        if (s_cgSpPhysicsGravity[i].cent && s_cgSpPhysicsGravity[i].cent == cent)
            break;
    }
    if (i == 32)
        i = -1;
    if (ragdollHandle > 0)
    {
        RagdollBody *body = Ragdoll_HandleBody(ragdollHandle);
        if (i < 0)
        {
            body->spPhysGravityOverride = false;
            return;
        }
        body->spPhysGravityOverride = true;
        body->spPhysGravity = s_cgSpPhysicsGravity[i].gravity;
    }
    if (i >= 0)
    {
        s_cgSpPhysicsGravity[i].cent = nullptr;
        s_cgSpPhysicsGravity[i].gravity = 0;
    }
}

// zombies: <ent> SetPhysicsGravity( <gravity> ) (SP C_m_setphysicsgravity 0x006507a0): update the entity's slot, else
// take the first free one; a full table drops the call. No entity check: the entref's centity (SP picks another
// struct for entnum >= 0x400; KB has no such client entities).
static void __cdecl CScr_SetPhysicsGravity(scr_entref_t entref)
{
    centity_s *cent = CG_GetEntity(entref.client, entref.entnum);
    int gravity = Scr_GetInt(0, SCRIPTINSTANCE_CLIENT);
    int i;

    for (i = 0; i < 32; ++i)
    {
        if (s_cgSpPhysicsGravity[i].cent == cent)
        {
            s_cgSpPhysicsGravity[i].gravity = gravity;
            return;
        }
    }
    for (i = 0; i < 32; ++i)
    {
        if (!s_cgSpPhysicsGravity[i].cent)
        {
            s_cgSpPhysicsGravity[i].cent = cent;
            s_cgSpPhysicsGravity[i].gravity = gravity;
            return;
        }
    }
}

// zombies: <ent> ClearPhysicsGravity() (SP C_m_clearphysicsgravity 0x0050c1f0): free the entity's slot.
static void __cdecl CScr_ClearPhysicsGravity(scr_entref_t entref)
{
    centity_s *cent = CG_GetEntity(entref.client, entref.entnum);

    for (int i = 0; i < 32; ++i)
    {
        if (s_cgSpPhysicsGravity[i].cent == cent)
        {
            s_cgSpPhysicsGravity[i].cent = nullptr;
            s_cgSpPhysicsGravity[i].gravity = 0;
            return;
        }
    }
}

// zombies: <ent> FireWeapon( [<weapon name>, <tag> [, <clientNum>]] ) (SP client method 0x00895f60): no entity check,
// local client 0's centity; "tag_flash" fires EV_FIRE_WEAPON, any other tag EV_FIRE_WEAPON_LEFT with tag_flash1.
static void __cdecl CScr_FireWeapon(scr_entref_t entref)
{
    int numParams = Scr_GetNumParam(SCRIPTINSTANCE_CLIENT);
    centity_s *cent = CG_GetEntity(0, entref.entnum);
    cg_s *cgameGlob = CG_GetLocalClientGlobals(0);
    int event = EV_FIRE_WEAPON;
    unsigned __int16 tag = scr_const.tag_flash;
    unsigned int weapon = 0;
    if (numParams)
    {
        if (numParams != 2 && numParams != 3)
        {
            Scr_Error(SCRIPTINSTANCE_CLIENT, "Incorrect number of params for 'FireWeapon'.  Either 2,3 or none.", 0);
            return;
        }
        weapon = BG_GetWeaponIndexForName(Scr_GetString(0, SCRIPTINSTANCE_CLIENT), 0);
        if (strcmp(Scr_GetString(1, SCRIPTINSTANCE_CLIENT), "tag_flash"))
        {
            event = EV_FIRE_WEAPON_LEFT;
            tag = scr_const.tag_flash1;
        }
        if (numParams == 3)
        {
            int clientNum = Scr_GetInt(2, SCRIPTINSTANCE_CLIENT);
            if (clientNum < 0 || clientNum > 4)
            {
                Scr_Error(SCRIPTINSTANCE_CLIENT, "Out-of-range client number specified for param 3 of 'FireWeapon'.", 0);
                return;
            }
            if (cgameGlob->clientNum == clientNum)
                return;
        }
    }
    if (CL_LocalClient_IsActive(0) && cent->nextValid)
        CG_FireWeapon(0, cent, event, tag, weapon, &cgameGlob->predictedPlayerState, 0);
}

// zombies: setsunlight( <r>, <g>, <b> [, <spec r>, <spec g>, <spec b>] ) (SP C_f_setsunlight 0x00895260): with other
// than 6 params the specular colour is the diffuse one; no count error.
static void __cdecl CScr_SetSunLight()
{
    float diffuse[3];
    float specular[3];
    diffuse[0] = Scr_GetFloat(0, SCRIPTINSTANCE_CLIENT);
    diffuse[1] = Scr_GetFloat(1, SCRIPTINSTANCE_CLIENT);
    diffuse[2] = Scr_GetFloat(2, SCRIPTINSTANCE_CLIENT);
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) == 6)
    {
        specular[0] = Scr_GetFloat(3, SCRIPTINSTANCE_CLIENT);
        specular[1] = Scr_GetFloat(4, SCRIPTINSTANCE_CLIENT);
        specular[2] = Scr_GetFloat(5, SCRIPTINSTANCE_CLIENT);
    }
    else
    {
        specular[0] = diffuse[0];
        specular[1] = diffuse[1];
        specular[2] = diffuse[2];
    }
    R_SetSunLightOverride(diffuse, specular);
}

// zombies: resetsunlight() (SP C_f_resetsunlight 0x00895310).
static void __cdecl CScr_ResetSunLight()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT))
        Scr_Error(SCRIPTINSTANCE_CLIENT, "Incorrect number of parameters\n", 0);
    R_ResetSunLightOverride();
}

// zombies: setvolfogforclient( <localClientNum>, <setvolfog params> ) (SP 0x00895610): setvolfog with a leading local
// client; returns before the value checks when it is > 1 or inactive; the fog lerp starts at client 0's time.
static void __cdecl CScr_SetVolFogForClient()
{
    int numParams = Scr_GetNumParam(SCRIPTINSTANCE_CLIENT);
    if (numParams != 19 && numParams != 9)
    {
        Scr_Error(
            SCRIPTINSTANCE_CLIENT,
            "Incorrect number of parameters\n"
            "USAGE: setVolFog(<localClientNum>, <startDist>, <halfwayDist>, <halfwayHeight>, <baseHeight>, <red>, <green>, <blue>, <fog color "
            "scale>, <sun red>, <sun green>, <sun blue>, <sun dir X>, <sun dir Y>, <sun dir Z>, <sun start angle>, <sun end "
            "angle>, <transition time>, <max fog opacity>)\n",
            0);
    }
    int localClientNum = Scr_GetInt(0, SCRIPTINSTANCE_CLIENT);
    if (localClientNum > 1 || !CL_LocalClient_IsActive(localClientNum))
        return;
    float startDist = Scr_GetFloat(1, SCRIPTINSTANCE_CLIENT);
    if (startDist < 0.0f)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "setExpFog: startDist must be greater or equal to 0", 0);
    float halfwayDist = Scr_GetFloat(2, SCRIPTINSTANCE_CLIENT);
    if (halfwayDist <= 0.0f)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "setVolFog: halfwayDist must be greater than 0", 0);
    float halfwayHeight = Scr_GetFloat(3, SCRIPTINSTANCE_CLIENT);
    if (halfwayHeight < 0.0f)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "setVolFog: halfwayHeight must be greater or equal to 0", 0);
    float baseHeight = Scr_GetFloat(4, SCRIPTINSTANCE_CLIENT);
    float density = 1.0f / halfwayDist;
    float heightDensity = halfwayHeight >= 1.0f ? 1.0f / halfwayHeight : 0.0f;
    float red = Scr_GetFloat(5, SCRIPTINSTANCE_CLIENT);
    float green = Scr_GetFloat(6, SCRIPTINSTANCE_CLIENT);
    float blue = Scr_GetFloat(7, SCRIPTINSTANCE_CLIENT);
    int transitionTime;
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) == 9)
    {
        transitionTime = (int)(Scr_GetFloat(8, SCRIPTINSTANCE_CLIENT) * 1000.0f);
        R_SetFogFromServer(localClientNum, startDist, red, green, blue, density, heightDensity, baseHeight,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        transitionTime = (int)(Scr_GetFloat(17, SCRIPTINSTANCE_CLIENT) * 1000.0f);
        R_SetFogFromServer(localClientNum, startDist, red, green, blue, density, heightDensity, baseHeight,
            Scr_GetFloat(8, SCRIPTINSTANCE_CLIENT),
            Scr_GetFloat(9, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(10, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(11, SCRIPTINSTANCE_CLIENT),
            Scr_GetFloat(12, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(13, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(14, SCRIPTINSTANCE_CLIENT),
            Scr_GetFloat(15, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(16, SCRIPTINSTANCE_CLIENT),
            Scr_GetFloat(18, SCRIPTINSTANCE_CLIENT));
    }
    R_SwitchFog(localClientNum, 1, CG_GetLocalClientGlobals(0)->time, transitionTime);
}

// zombies: getorigin (SP 0x008964d0): the centity's pose origin (cent+0x24 in SP).
static void __cdecl CScr_GetOrigin(scr_entref_t entref)
{
    centity_s *cent = CG_GetEntity(entref.client, entref.entnum);
    float origin[3];
    origin[0] = cent->pose.origin[0];
    origin[1] = cent->pose.origin[1];
    origin[2] = cent->pose.origin[2];
    Scr_AddVector(origin, SCRIPTINSTANCE_CLIENT);
}

// zombies: getplayerangles (SP 0x00896670): SP returns its one cg's refdef view angles
// (cg+0xa4620, what CG_CalcViewValues turns into the view axis) whatever entity it is called on.
static void __cdecl CScr_GetPlayerAngles(scr_entref_t entref)
{
    Scr_AddVector(CG_GetLocalClientGlobals(0)->refdefViewAngles, SCRIPTINSTANCE_CLIENT);
}

// zombies: geteye (SP 0x00896540): the view origin (cg+0x8c120), pushed cg_cameraWaterClip off the
// water surface when it is closer to it than that - the same clip CG_CalcViewValues applies. Like
// getplayerangles it reads the one cg, whatever entity it is called on.
static void __cdecl CScr_GetEye(scr_entref_t entref)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(0);
    float eye[3];
    eye[0] = cgameGlob->refdef.vieworg[0];
    eye[1] = cgameGlob->refdef.vieworg[1];
    eye[2] = cgameGlob->refdef.vieworg[2];
    float waterHeight = (float)CM_GetWaterHeight(cgameGlob->refdef.vieworg, 200.0f, -200.0f);
    float clip = cg_cameraWaterClip->current.value;
    float delta = eye[2] - waterHeight;
    if (clip > fabsf(delta))
    {
        if (delta > 0.0f)
            eye[2] = clip + waterHeight;
        else
            eye[2] = waterHeight - clip;
    }
    Scr_AddVector(eye, SCRIPTINSTANCE_CLIENT);
}

// zombies: the entity a centity is linked to (SP 0x0040b450), 1023 for none. SP answers for three
// entity types:
//  - script movers: the entity-state short at cent+0x1ce, which SP resets to 0x3ff - KB's
//    scriptMover.attachedEntNum is that field (identified by its role, not by offset);
//  - actors (SP eType 0x10, KB ET_ACTOR 0x11): a per-actor delayed link record (0x00540d20 +0x3fc,
//    promoted from +0x400 once cg time reaches +0x404) that KB's actorInfo_t does not carry, so
//    this port answers 1023 for actors (APPROXIMATED);
//  - players: clientinfo+0x56c, the attached vehicle's entity number (clientInfo_t's
//    attachedVehEntNum, identified by its place in the struct).
static int CG_SP_GetLinkedEntNum(int localClientNum, const centity_s *cent)
{
    if (cent->nextState.eType == ET_SCRIPTMOVER)
        return cent->nextState.lerp.u.scriptMover.attachedEntNum;
    if (cent->nextState.eType == ET_ACTOR)
        return 1023;
    if (cent->nextState.eType == ET_PLAYER)
        return CG_GetLocalClientGlobals(localClientNum)->bgs.clientinfo[cent->nextState.clientNum].attachedVehEntNum;
    return 1023;
}

// zombies: getlinkedent (SP 0x00896a70): the linked entity, or nothing.
static void __cdecl CScr_GetLinkedEnt(scr_entref_t entref)
{
    centity_s *cent = CG_GetEntity(entref.client, entref.entnum);
    int linkedEntNum = CG_SP_GetLinkedEntNum(entref.client, cent);
    if (linkedEntNum != 1023)
        CScr_AddEntity(CG_GetEntity(entref.client, linkedEntNum), entref.client);
}

// zombies: SP's one-entry tag cache (0x02ff8350 time, 0x02ff8354 entity, 0x02ff8358 tag,
// 0x02ff835c axis, 0x02ff8380 origin), shared by its client tag methods.
static struct
{
    int time;
    int entnum;
    unsigned short tagName;
    float axis[3][3];
    float origin[3];
} cscr_spTagCache;

// zombies: gettagforwardvector (SP 0x00416680): the tag's world forward axis. Nothing is returned
// when the entity has no DObj or no such tag.
static void __cdecl CScr_GetTagForwardVector(scr_entref_t entref)
{
    centity_s *cent = CScr_SP_GetMethodEntity(entref);
    unsigned int tagName = CScr_GetConstServerString(0);
    cg_s *cgameGlob = CG_GetLocalClientGlobals(entref.client);
    if (cent->nextState.number != cscr_spTagCache.entnum || cgameGlob->time != cscr_spTagCache.time
        || tagName != cscr_spTagCache.tagName)
    {
        DObj *obj = Com_GetClientDObj(cent->nextState.number, cent->pose.localClientNum);
        if (!obj)
            return;
        if (!CG_DObjGetWorldTagMatrix(&cent->pose, obj, tagName, cscr_spTagCache.axis, cscr_spTagCache.origin))
            return;
        cscr_spTagCache.entnum = cent->nextState.number;
        cscr_spTagCache.time = cgameGlob->time;
        cscr_spTagCache.tagName = (unsigned short)tagName;
    }
    Scr_AddVector(cscr_spTagCache.axis[0], SCRIPTINSTANCE_CLIENT);
}

// zombies: getnormalizedmovement (SP 0x00640bd0): the local client's current move command,
// (forwardmove, rightmove, 0) / 127, from the client's pmove (SP 0x02ff7980 + client * 0x918,
// pm->cmd at +4). The entity itself is not read.
static void __cdecl CScr_GetNormalizedMovement(scr_entref_t entref)
{
    const usercmd_s *cmd = &cg_pmove[entref.client].cmd;
    float movement[3];
    movement[0] = (float)cmd->forwardmove * 0.0078740157f;
    movement[1] = (float)cmd->rightmove * 0.0078740157f;
    movement[2] = 0.0f;
    Scr_AddVector(movement, SCRIPTINSTANCE_CLIENT);
}

// zombies: isspectating, the method form (SP 0x00897410): a player entity while the local client is
// a spectator (pm_type 4) or following someone (otherFlags & 2).
static void __cdecl CScr_IsSpectatingMethod(scr_entref_t entref)
{
    centity_s *cent = CScr_SP_GetMethodEntity(entref);
    const playerState_s *ps = &CG_GetLocalClientGlobals(0)->predictedPlayerState;
    if (cent->nextState.eType == ET_PLAYER && (ps->pm_type == 4 || (ps->otherFlags & 2) != 0))
        Scr_AddInt(1, SCRIPTINSTANCE_CLIENT);
    else
        Scr_AddInt(0, SCRIPTINSTANCE_CLIENT);
}

// zombies: swimming (SP 0x008966d0): the predicted player's water level (PM_GetWaterLevel against the
// water height at its origin) is 3 or more. It reads the one cg, whatever entity it is called on.
static void __cdecl CScr_Swimming(scr_entref_t entref)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(0);
    float waterHeight = (float)CM_GetWaterHeight(cgameGlob->predictedPlayerState.origin, 200.0f, -200.0f);
    if (PM_GetWaterLevel(&cgameGlob->predictedPlayerState, waterHeight) >= 3)
        Scr_AddInt(1, SCRIPTINSTANCE_CLIENT);
    else
        Scr_AddInt(0, SCRIPTINSTANCE_CLIENT);
}

// zombies: getwaterheight (SP 0x0061a050): the water height at a point, like the server's.
static void __cdecl CScr_GetWaterHeight()
{
    float pos[3];
    Scr_GetVector(0, pos, SCRIPTINSTANCE_CLIENT);
    Scr_AddFloat((float)CM_GetWaterHeight(pos, 200.0f, -200.0f), SCRIPTINSTANCE_CLIENT);
}

// zombies: SP's search for the local client that owns a player entity (inlined in SP 0x00896be0,
// 0x00896c40, 0x00896cc0, 0x00896d40): an active local client with a next snapshot whose
// clientNum is the entity. -1 when none.
static int CScr_SP_LocalClientForEntity(int entnum)
{
    for (int localClientNum = 0; localClientNum < MAX_LOCAL_CLIENTS; ++localClientNum)
    {
        if (!CL_LocalClient_IsActive(localClientNum))
            continue;
        cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
        if (cgameGlob->nextSnap && cgameGlob->clientNum == entnum)
            return localClientNum;
    }
    return -1;
}

// zombies: getlocalclientnumber (SP 0x00896be0): the local client number of a local player;
// nothing when no active local client owns it.
static void __cdecl CScr_GetLocalClientNumber(scr_entref_t entref)
{
    if (!CG_IsLocalPlayer(entref.entnum))
    {
        Scr_Error(SCRIPTINSTANCE_CLIENT, "You can only call GetLocalClientNumber on players.", false);
        return;
    }
    int localClientNum = CScr_SP_LocalClientForEntity(entref.entnum);
    if (localClientNum >= 0)
        Scr_AddInt(CG_GetLocalClientGlobals(localClientNum)->localClientNum, SCRIPTINSTANCE_CLIENT);
}

// zombies: setblur, the client method (SP 0x00896d40): self setblur(<radius>, <seconds>) blurs the
// local player's view to <radius> over <seconds> (truncated to ms), SP 0x0055e720 with time mode 0
// and priority 1. The not-a-player error is SP's own text, copied from getlocalclientnumber.
static void __cdecl CScr_SetBlur(scr_entref_t entref)
{
    float radius = (float)Scr_GetFloat(0, SCRIPTINSTANCE_CLIENT);
    float time = (float)Scr_GetFloat(1, SCRIPTINSTANCE_CLIENT);
    if (time < 0.0f)
        Scr_ParamError(1, "Time must be positive", SCRIPTINSTANCE_CLIENT);
    int duration = (int)(time * 1000.0f);
    if (radius < 0.0f)
        Scr_ParamError(0, "Blur value must be greater than 0", SCRIPTINSTANCE_CLIENT);
    if (!CG_IsLocalPlayer(entref.entnum))
    {
        Scr_Error(SCRIPTINSTANCE_CLIENT, "You can only call GetLocalClientNumber on players.", false);
        return;
    }
    int localClientNum = CScr_SP_LocalClientForEntity(entref.entnum);
    if (localClientNum >= 0)
        CG_SP_StartClientBlur(localClientNum, duration, radius, 0, 1);
}

// zombies: linktocamera (SP 0x00896b10): adds the entity to the camera's linked list (up to 4) with
// a control type, FULL_CTRL (4) by default. KB's cg_s already holds that list
// (cameraLinkedEntities*), but nothing in BO1Zombies reads it yet: SP's readers (0x004578b0,
// 0x00490840, 0x004e3880, 0x0050b6f0, 0x005c3420, 0x0064f710, 0x00776540, 0x00777dd0) are not ported.
static void __cdecl CScr_LinkToCamera(scr_entref_t entref)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(0);
    int linkType = FULL_CTRL;
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) == 1)
        linkType = Scr_GetInt(0, SCRIPTINSTANCE_CLIENT);
    if (cgameGlob->cameraLinkedEntitiesCount < 4)
    {
        cgameGlob->cameraLinkedEntities[cgameGlob->cameraLinkedEntitiesCount] = entref.entnum;
        cgameGlob->cameraLinkedEntitiesType[cgameGlob->cameraLinkedEntitiesCount] = (link_type_e)linkType;
        ++cgameGlob->cameraLinkedEntitiesCount;
    }
    else
    {
        Com_PrintWarning(14, "WARNING: Max camera linked entities reached. You can link up to %d entities to the camera.\n", 4);
    }
}

// zombies: setshaderconstant (SP 0x00896380): writes one float4 of the entity's shader constant set
// (only a slot mapshaderconstant mapped; R_SetShaderConstantSetValue is SP's SetShaderConstant
// 0x006c9430) and, when the entity is the local player, of the view model's. Returns nothing.
// SP also calls CG_GetClientNumForLocalClient(entref.client) first and drops the result.
static void __cdecl CScr_SetShaderConstant(scr_entref_t entref)
{
    if (entref.classnum)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "not an entity", false);
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 6)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "USAGE: ent setshaderconstant( <localClientNum>, <index>, <x>, <y>, <z>, <w>)\n", false);
    int localClientNum = CScr_GetLocalClientNum(0);
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    unsigned int index = Scr_GetInt(1, SCRIPTINSTANCE_CLIENT);
    float value[4];
    value[0] = (float)Scr_GetFloat(2, SCRIPTINSTANCE_CLIENT);
    value[1] = (float)Scr_GetFloat(3, SCRIPTINSTANCE_CLIENT);
    value[2] = (float)Scr_GetFloat(4, SCRIPTINSTANCE_CLIENT);
    value[3] = (float)Scr_GetFloat(5, SCRIPTINSTANCE_CLIENT);
    centity_s *cent = CG_GetEntity(localClientNum, entref.entnum);
    if (!cent)
        return;
    R_SetShaderConstantSetValue(&cent->pose.constantSet, index, value);
    if (cgameGlob->nextSnap && cgameGlob->nextSnap->ps.clientNum == entref.entnum)
        R_SetShaderConstantSetValue(&cgameGlob->viewModelPose.constantSet, index, value);
}

// zombies: SP's seconds -> milliseconds for vision set durations: the product is stored as a float,
// the double epsilon added, then FISTP (round to nearest).
static int CScr_SP_VisionSeconds(unsigned int index)
{
    float ms = (float)(Scr_GetFloat(index, SCRIPTINSTANCE_CLIENT) * 1000.0f);
    return _mm_cvtsd_si32(_mm_set_sd((double)ms + 9.313225746154785e-10));
}

// zombies: setvolfog (SP 0x00895340): BO1Zombies's CScr_SetClientVolumetricFog checks, but no cg_clientVolFog store:
// when local client 0 is active (checked after the value checks, 0x00895457) CG_SetFogVars (SP 0x006ed370 =
// R_SetFogFromServer) then CG_StartFogLerp (SP 0x006ed580 = R_SwitchFog(0, 1, cg time, ms)). 18 params: p7 fog
// colour scale, p16 transition seconds, p17 max opacity; 8 params: p7 transition seconds, scale 1, no sun fog,
// opacity 1. The startDist message keeps the exe's "setExpFog" typo.
static void __cdecl CScr_SetVolFog()
{
    int numParams = Scr_GetNumParam(SCRIPTINSTANCE_CLIENT);
    if (numParams != 18 && numParams != 8)
    {
        Scr_Error(
            SCRIPTINSTANCE_CLIENT,
            "Incorrect number of parameters\n"
            "USAGE: setVolFog(<startDist>, <halfwayDist>, <halfwayHeight>, <baseHeight>, <red>, <green>, <blue>, <fog color "
            "scale>, <sun red>, <sun green>, <sun blue>, <sun dir X>, <sun dir Y>, <sun dir Z>, <sun start angle>, <sun end "
            "angle>, <transition time>, <max fog opacity>)\n",
            0);
    }
    float startDist = Scr_GetFloat(0, SCRIPTINSTANCE_CLIENT);
    if (startDist < 0.0f)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "setExpFog: startDist must be greater or equal to 0", 0);
    float halfwayDist = Scr_GetFloat(1, SCRIPTINSTANCE_CLIENT);
    if (halfwayDist <= 0.0f)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "setVolFog: halfwayDist must be greater than 0", 0);
    float halfwayHeight = Scr_GetFloat(2, SCRIPTINSTANCE_CLIENT);
    if (halfwayHeight < 0.0f)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "setVolFog: halfwayHeight must be greater or equal to 0", 0);
    float baseHeight = Scr_GetFloat(3, SCRIPTINSTANCE_CLIENT);
    float density = 1.0f / halfwayDist;
    float heightDensity = halfwayHeight >= 1.0f ? 1.0f / halfwayHeight : 0.0f;
    float red = Scr_GetFloat(4, SCRIPTINSTANCE_CLIENT);
    float green = Scr_GetFloat(5, SCRIPTINSTANCE_CLIENT);
    float blue = Scr_GetFloat(6, SCRIPTINSTANCE_CLIENT);
    if (!CL_LocalClient_IsActive(0))
        return;
    int transitionTime;
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) == 8)
    {
        transitionTime = (int)(Scr_GetFloat(7, SCRIPTINSTANCE_CLIENT) * 1000.0f);
        R_SetFogFromServer(0, startDist, red, green, blue, density, heightDensity, baseHeight,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        transitionTime = (int)(Scr_GetFloat(16, SCRIPTINSTANCE_CLIENT) * 1000.0f);
        R_SetFogFromServer(0, startDist, red, green, blue, density, heightDensity, baseHeight,
            Scr_GetFloat(7, SCRIPTINSTANCE_CLIENT),
            Scr_GetFloat(8, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(9, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(10, SCRIPTINSTANCE_CLIENT),
            Scr_GetFloat(11, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(12, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(13, SCRIPTINSTANCE_CLIENT),
            Scr_GetFloat(14, SCRIPTINSTANCE_CLIENT), Scr_GetFloat(15, SCRIPTINSTANCE_CLIENT),
            Scr_GetFloat(17, SCRIPTINSTANCE_CLIENT));
    }
    R_SwitchFog(0, 1, CG_GetLocalClientGlobals(0)->time, transitionTime);
}

// zombies: getvisionsetnaked(<localClientNum>) (SP 0x008949a0): reads the int and ignores it, then returns local
// client 0's naked vision name (SP cg+0xcc5a0, written only by the vision configstring, 0x006929e0; the csc
// visionsetnaked below does not store it).
static void __cdecl CScr_GetVisionSetNaked()
{
    Scr_GetInt(0, SCRIPTINSTANCE_CLIENT);
    Scr_AddString(CG_GetLocalClientGlobals(0)->visionNameNaked, SCRIPTINSTANCE_CLIENT);
}

// zombies: visionsetnaked (SP 0x008948e0): VisionSetNaked(<localClientNum>, <name> [, <seconds>]),
// CG_VisionSetStartLerp(localClientNum, naked, smooth, name, ms), 1 s by default. SP does not store
// the name as the naked name.
static void __cdecl CScr_VisionSetNaked()
{
    int duration = 1000;
    unsigned int numParam = Scr_GetNumParam(SCRIPTINSTANCE_CLIENT);
    if (numParam != 2)
    {
        if (numParam != 3)
        {
            Scr_Error(SCRIPTINSTANCE_CLIENT, "VisionSetNaked() called with wrong params.\n", false);
            return;
        }
        duration = CScr_SP_VisionSeconds(2);
    }
    int localClientNum = CScr_GetLocalClientNum(0);
    const char *name = Scr_GetString(1, SCRIPTINSTANCE_CLIENT);
    CG_SP_VisionSetStartLerp(localClientNum, VISIONSETMODE_NAKED, VISIONSETLERP_TO_SMOOTH, name, duration);
}

// zombies: visionsetdamage (SP 0x008949c0): VisionSetDamage(<localClientNum>, <on>, <name> [, <seconds>]).
// On: the name goes to channel 6's name (cg+0xcc720), channel 7 holds until turned off and lerps to
// the set named in channel 8's name (cg+0xcc7a0, "low_health" from CG_InitVisionSets). Off: channel
// 6's name becomes the naked name and channel 7 lerps to the naked set, held until the lerp ends.
// That is what the exe does (the script's name is not the one channel 7 lerps to).
static void __cdecl CScr_VisionSetDamage()
{
    int duration = 1000;
    CScr_GetLocalClientNum(0);
    cg_s *cgameGlob = CG_GetLocalClientGlobals(0);
    CgSPClientExt *ext = CG_SP_GetClientExt(0);
    int on = Scr_GetInt(1, SCRIPTINSTANCE_CLIENT);
    const char *name = Scr_GetString(2, SCRIPTINSTANCE_CLIENT);
    unsigned int numParam = Scr_GetNumParam(SCRIPTINSTANCE_CLIENT);
    if (numParam != 3)
    {
        if (numParam != 4)
        {
            Scr_Error(SCRIPTINSTANCE_CLIENT, "VisionSetDamage() called with wrong params.\n", false);
            return;
        }
        duration = CScr_SP_VisionSeconds(3);
    }
    char *lastStandName = ext->visionName[SP_VISIONSET_LASTSTAND - SP_VISIONSET_FIRST];
    if (on)
    {
        ext->damageVisionEndTime = 0x7FFFFFFF;
        I_strncpyz(lastStandName, name, 64);
        CG_SP_VisionSetStartLerp(cgameGlob->localClientNum, SP_VISIONSET_DEATH, VISIONSETLERP_TO_SMOOTH,
            ext->visionName[SP_VISIONSET_LOWHEALTH - SP_VISIONSET_FIRST], duration);
    }
    else
    {
        ext->damageVisionEndTime = cgameGlob->time + duration;
        I_strncpyz(lastStandName, cgameGlob->visionNameNaked, 64);
        CG_SP_VisionSetStartLerp(cgameGlob->localClientNum, SP_VISIONSET_DEATH, VISIONSETLERP_TO_SMOOTH,
            cgameGlob->visionNameNaked, duration);
    }
}

// zombies: visionsetunderwater (SP 0x00894800): VisionSetUnderWater(<localClientNum>, <name> [, <seconds>]),
// channel 9 (drawn while the view is under water); the name is kept once the set loads.
static void __cdecl CScr_VisionSetUnderWater()
{
    int duration = 1000;
    unsigned int numParam = Scr_GetNumParam(SCRIPTINSTANCE_CLIENT);
    if (numParam != 2)
    {
        if (numParam != 3)
        {
            Scr_Error(SCRIPTINSTANCE_CLIENT, "visionsetunderwater() called with wrong params.\n", false);
            return;
        }
        duration = CScr_SP_VisionSeconds(2);
    }
    int localClientNum = CScr_GetLocalClientNum(0);
    const char *name = Scr_GetString(1, SCRIPTINSTANCE_CLIENT);
    if (CG_SP_VisionSetStartLerp(localClientNum, SP_VISIONSET_UNDERWATER, VISIONSETLERP_TO_SMOOTH, name, duration))
        I_strncpyz(CG_SP_GetClientExt(0)->visionName[SP_VISIONSET_UNDERWATER - SP_VISIONSET_FIRST], name, 64);
}

// zombies: setwaterfog (SP 0x00895900): SetWaterFog(<startDist>, <halfwayDist>, <halfwayHeight>,
// <baseHeight>, <r>, <g>, <b>, <opacity>, <sunR>, <sunG>, <sunB>, <sunDir x, y, z>, <sunStartAng>,
// <sunEndAng>, <sunMaxOpacity>) - 17 arguments, or the old 7 (sun off, opacity 1). Written straight
// into the refdef's water fog (SP cg+0xa4584, KB refdef.waterFog, the same WaterFogDef layout);
// nothing in BO1Zombies's renderer reads refdef.waterFog yet.
static void __cdecl CScr_SetWaterFog()
{
    unsigned int numParam = Scr_GetNumParam(SCRIPTINSTANCE_CLIENT);
    if (numParam != 7 && numParam != 17)
        Scr_Error(SCRIPTINSTANCE_CLIENT,
            "Incorrect number of parameters\nUSAGE: SetWaterFog(<startDist>, <halfwayDist>, <halfwayHeight>, <baseHeight>, <red>, <green>, <blue>)\n",
            false);
    float startDist = (float)Scr_GetFloat(0, SCRIPTINSTANCE_CLIENT);
    float halfwayDist = (float)Scr_GetFloat(1, SCRIPTINSTANCE_CLIENT);
    if (halfwayDist <= 0.0f)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "SetWaterFog: halfwayDist must be greater than 0", false);
    float halfwayHeight = (float)Scr_GetFloat(2, SCRIPTINSTANCE_CLIENT);
    if (halfwayHeight < 0.0f)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "SetWaterFog: halfwayHeight must be greater or equal to 0", false);
    float baseHeight = (float)Scr_GetFloat(3, SCRIPTINSTANCE_CLIENT);
    float density = 1.0f / halfwayDist;
    float heightDensity = halfwayHeight >= 1.0f ? 1.0f / halfwayHeight : 0.0f;
    float red = (float)Scr_GetFloat(4, SCRIPTINSTANCE_CLIENT);
    float green = (float)Scr_GetFloat(5, SCRIPTINSTANCE_CLIENT);
    float blue = (float)Scr_GetFloat(6, SCRIPTINSTANCE_CLIENT);
    WaterFogDef *fog = &CG_GetLocalClientGlobals(0)->refdef.waterFog;
    fog->startTime = 0;
    fog->finishTime = 0;
    fog->color[0] = red;
    fog->color[3] = 1.0f;
    fog->color[1] = green;
    fog->color[2] = blue;
    fog->fogStart = startDist;
    fog->density = density;
    fog->heightDensity = heightDensity;
    fog->baseHeight = baseHeight;
    if (numParam == 7)
    {
        Com_Printf(1, "setWaterFog: Old syntax used. Please update script.\n");
        fog->sunFogStartAng = 0.0f;
        fog->sunFogEndAng = 0.0f;
        fog->sunFogColor[0] = 1.0f;
        fog->sunFogColor[1] = 1.0f;
        fog->sunFogColor[2] = 1.0f;
        fog->sunFogColor[3] = 1.0f;
        fog->sunFogDir[0] = 1.0f;
        fog->sunFogDir[1] = 0.0f;
        fog->sunFogDir[2] = 0.0f;
        return;
    }
    fog->color[3] = (float)Scr_GetFloat(7, SCRIPTINSTANCE_CLIENT);
    float sunMaxOpacity = (float)Scr_GetFloat(16, SCRIPTINSTANCE_CLIENT);
    float sunBlue = (float)Scr_GetFloat(10, SCRIPTINSTANCE_CLIENT);
    float sunGreen = (float)Scr_GetFloat(9, SCRIPTINSTANCE_CLIENT);
    fog->sunFogColor[0] = (float)Scr_GetFloat(8, SCRIPTINSTANCE_CLIENT);
    fog->sunFogColor[1] = sunGreen;
    fog->sunFogColor[2] = sunBlue;
    fog->sunFogColor[3] = sunMaxOpacity;
    float sunDirZ = (float)Scr_GetFloat(13, SCRIPTINSTANCE_CLIENT);
    float sunDirY = (float)Scr_GetFloat(12, SCRIPTINSTANCE_CLIENT);
    fog->sunFogDir[0] = (float)Scr_GetFloat(11, SCRIPTINSTANCE_CLIENT);
    fog->sunFogDir[1] = sunDirY;
    fog->sunFogDir[2] = sunDirZ;
    fog->sunFogStartAng = (float)Scr_GetFloat(14, SCRIPTINSTANCE_CLIENT);
    fog->sunFogEndAng = (float)Scr_GetFloat(15, SCRIPTINSTANCE_CLIENT);
}

// zombies: usealternateaimparams / clearalternateaimparams (SP 0x00896c40 / 0x00896cc0): the
// deadshot perk's aim-assist switch (SP cg+0xce534, kept in the SP side table), zombiemode only.
// Its readers - SP's aim assist and aim-target tag choice (0x007562c0, 0x00756db0, 0x00757460,
// 0x00757e60, 0x0077d300, 0x00792010), gamepad-only on PC - are not ported, so the flag has no
// effect yet.
static void CScr_SP_SetAlternateAimParams(scr_entref_t entref, int value, const char *notPlayerError, const char *noEffectError)
{
    if (!CG_IsLocalPlayer(entref.entnum))
    {
        Scr_Error(SCRIPTINSTANCE_CLIENT, notPlayerError, false);
        return;
    }
    if (!zombiemode->current.enabled)
        return;
    int localClientNum = CScr_SP_LocalClientForEntity(entref.entnum);
    if (localClientNum < 0)
    {
        Scr_Error(SCRIPTINSTANCE_CLIENT, noEffectError, false);
        return;
    }
    CG_SP_GetClientExt(localClientNum)->useAlternateAimParams = value;
}

static void __cdecl CScr_UseAlternateAimParams(scr_entref_t entref)
{
    CScr_SP_SetAlternateAimParams(entref, 1, "You can only call UseAlternateAim on players.",
        "UseAlternateAimParams had no effect...");
}

static void __cdecl CScr_ClearAlternateAimParams(scr_entref_t entref)
{
    CScr_SP_SetAlternateAimParams(entref, 0, "You can only call ClearAlternateAim on players.",
        "ClearAlternateAimParams had no effect...");
}

// zombies: updategamerprofile (SP 0x00894c30): writes the profile from the dvars, if changed.
static void __cdecl CScr_UpdateGamerProfile()
{
    int localClientNum = CScr_GetLocalClientNum(0);
    GamerProfile_UpdateProfileFromDvars(Com_LocalClient_GetControllerIndex(localClientNum), PROFILE_WRITE_IF_CHANGED);
}

// zombies: updatedvarsfromprofile (SP 0x00894c80): sets the profile dvars from the profile.
static void __cdecl CScr_UpdateDvarsFromProfile()
{
    int localClientNum = CScr_GetLocalClientNum(0);
    GamerProfile_UpdateDvarsFromProfile(Com_LocalClient_GetControllerIndex(localClientNum));
}

// zombies: setcollectible (SP 0x00894d60 -> 0x00485910): marks collectible <n> (1-based) in the
// bg_collectibles string ('1' at n - 1, no range check, as in SP) and queues "updategamerprofile".
// SP registers bg_collectibles at startup (0x0055c5b0: 49 '0's, flags 1) and its gamer profile
// stores it; BO1Zombies has neither, so the dvar is registered here on first use and the profile
// does not keep it.
static void __cdecl CScr_SetCollectible()
{
    int index = Scr_GetInt(0, SCRIPTINSTANCE_CLIENT);
    const dvar_s *collectibles = Dvar_FindVar("bg_collectibles");
    if (!collectibles)
        collectibles = _Dvar_RegisterString("bg_collectibles", "0000000000000000000000000000000000000000000000000", 1, "");
    char value[52];
    I_strncpyz(value, collectibles->current.string, 50);
    value[index - 1] = '1';
    Dvar_SetString((dvar_s *)collectibles, value);
    Cbuf_InsertText(0, "updategamerprofile\n");
}

// zombies: client setclientdvar( <name>, <value> ) (SP C_f_setclientdvar 0x00894ae0); the SP front end's
// clientscripts/frontend.csc calls it. SP 0x0062a0b0 is Dvar_SetFromStringByName.
static void __cdecl CScr_SetClientDvar()
{
    const char *dvarName = Scr_GetString(0, SCRIPTINSTANCE_CLIENT);
    if (!dvarName || !*dvarName)
        Scr_ParamError(0, "SetClientDvar: unknown dvar name", SCRIPTINSTANCE_CLIENT);
    char *value = Scr_GetString(1, SCRIPTINSTANCE_CLIENT);
    if (!value || !*value)
        Scr_ParamError(1, "SetClientDvar: unknown dvar value", SCRIPTINSTANCE_CLIENT);
    Dvar_SetFromStringByName(dvarName, value);
}

// zombies: the SP front end's extra camera (clientscripts/frontend.csc). SP keeps the camera entity at
// cg+0xa46cc (KB: cg_s::extraCamEntity, the field MP's setextracamentity writes; 1023 = none) and its fov at
// cg+0xa46d0 (KB: cg_s::cameraData.extraCamFov, the matching SP-layout pair; KB's extra cam renderer reads
// cg_fovExtraCam instead, so the fov is kept but not yet used). The local client number is param 0.
// <ent> isextracam( <localClientNum> ) (SP 0x00895b60).
static void __cdecl CScr_IsExtraCam(scr_entref_t entref)
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 1)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "isextracam() called with wrong params.\n", false);
    cg_s *cgameGlob = CG_GetLocalClientGlobals(CScr_GetLocalClientNum(0));
    if (cgameGlob->extraCamEntity != 1023)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "There can be only one extra camera active in the level.", false);
    cgameGlob->extraCamEntity = entref.entnum;
}

// stopextracam( <localClientNum> ) (SP 0x00895bf0): back to none and SP's default fov 65.
static void __cdecl CScr_StopExtraCam()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 1)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "CScr_StopExtraCam() called with wrong params.\n", false);
    cg_s *cgameGlob = CG_GetLocalClientGlobals(CScr_GetLocalClientNum(0));
    if (cgameGlob->extraCamEntity != 1023)
    {
        cgameGlob->extraCamEntity = 1023;
        cgameGlob->cameraData.extraCamFov = 65.0f; // SP .rdata 0x009ac544
    }
}

// setextracamfov( <localClientNum>, <fov> ) (SP 0x00895c70).
static void __cdecl CScr_SetExtraCamFov()
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 2)
        Scr_Error(SCRIPTINSTANCE_CLIENT, "CScr_SetExtraCamFov() called with wrong params.\n", false);
    float fov = Scr_GetFloat(1, SCRIPTINSTANCE_CLIENT);
    cg_s *cgameGlob = CG_GetLocalClientGlobals(CScr_GetLocalClientNum(0));
    if (cgameGlob->extraCamEntity != 1023)
        cgameGlob->cameraData.extraCamFov = fov;
    else
        Scr_Error(SCRIPTINSTANCE_CLIENT, "CScr_SetExtraCamFov(), There is no extra cam active in the level.\n", false);
}

// zombies: givegamerpicture (SP 0x00651a30): one of SP's shared empty builtins; it does nothing.
static void __cdecl CScr_GiveGamerPicture()
{
}

// zombies: ShowScores( localClientNum ) / HideScores( localClientNum ) (SP 0x00894720 / 0x00894790, zombietron's
// client script). Both set cg.showScores (SP cg+0xa9b38) on SP's one cgame global whatever the index: the
// bad-index error does not return.
static void CScr_SetShowScores(int show, const char *paramsError)
{
    if (Scr_GetNumParam(SCRIPTINSTANCE_CLIENT) != 1)
        Scr_Error(SCRIPTINSTANCE_CLIENT, paramsError, 0);
    int localClientNum = Scr_GetInt(0, SCRIPTINSTANCE_CLIENT);
    if (localClientNum < 0 || localClientNum >= 1)
        Scr_Error(SCRIPTINSTANCE_CLIENT, va("Trying to get a local client index for a client '%d' that is not a local client.", localClientNum), 0);
    CG_GetLocalClientGlobals(0)->showScores = show;
}

static void __cdecl CScr_ShowScores()
{
    CScr_SetShowScores(1, "ShowScores() called with wrong params.\n");
}

static void __cdecl CScr_HideScores()
{
    CScr_SetShowScores(0, "HideScores() called with wrong params.\n");
}

const BuiltinFunctionDef g_csp_client_functions[] =
{
    { "setsunlight", CScr_SetSunLight, 0 },
    { "resetsunlight", CScr_ResetSunLight, 0 },
    { "setvolfogforclient", CScr_SetVolFogForClient, 0 },
    { "triggerfx", CScr_TriggerFX, 0 },
    { "setclientdvar", CScr_SetClientDvar, 0 },
    { "stopextracam", CScr_StopExtraCam, 0 },
    { "setextracamfov", CScr_SetExtraCamFov, 0 },
    { "createdynentandlaunch", CScr_CreateDynEntAndLaunch, 0 },
    { "getdynent", CScr_GetDynEnt, 0 },
    { "forcegamemodemappings", CScr_ForceGameModeMappings, 0 },
    { "getlocalclienthealth", CScr_GetLocalClientHealth, 0 },
    { "getlocalclientmaxhealth", CScr_GetLocalClientMaxHealth, 0 },
    { "getweaponammoclip", CScr_GetWeaponAmmoClip, 0 },
    { "isads", CScr_IsADS, 0 },
    { "isthrowinggrenade", CScr_IsThrowingGrenade, 0 },
    { "ismeleeing", CScr_IsMeleeing, 0 },
    { "isonturret", CScr_IsOnTurret, 0 },
    { "getwaterheight", CScr_GetWaterHeight, 0 },
    { "visionsetnaked", CScr_VisionSetNaked, 0 },
    { "getvisionsetnaked", CScr_GetVisionSetNaked, 0 },
    { "setvolfog", CScr_SetVolFog, 0 },
    { "visionsetdamage", CScr_VisionSetDamage, 0 },
    { "visionsetunderwater", CScr_VisionSetUnderWater, 0 },
    { "setwaterfog", CScr_SetWaterFog, 0 },
    { "updategamerprofile", CScr_UpdateGamerProfile, 0 },
    { "updatedvarsfromprofile", CScr_UpdateDvarsFromProfile, 0 },
    { "setcollectible", CScr_SetCollectible, 0 },
    { "givegamerpicture", CScr_GiveGamerPicture, 0 },
    { "showscores", CScr_ShowScores, 0 },
    { "hidescores", CScr_HideScores, 0 },
    { nullptr, nullptr, 0 } // end marker, keep last
};
const unsigned int g_csp_client_function_count = ARRAY_COUNT(g_csp_client_functions) - 1;

const BuiltinMethodDef g_csp_client_methods[] =
{
    { "haseyes", CScr_HasEyes, 0 },
    { "isextracam", CScr_IsExtraCam, 0 },
    { "mapshaderconstant", CScr_MapShaderConstant, 0 },
    { "useweaponhidetags", CScr_UseWeaponHideTags, 0 },
    { "entgetsweaponfirenotification", CScr_EntGetsWeaponFireNotification, 0 },
    { "entyawoverrideslinkyaw", CScr_EntYawOverridesLinkYaw, 0 },
    { "fireweapon", CScr_FireWeapon, 0 },
    { "getorigin", CScr_GetOrigin, 0 },
    { "getplayerangles", CScr_GetPlayerAngles, 0 },
    { "geteye", CScr_GetEye, 0 },
    { "getlinkedent", CScr_GetLinkedEnt, 0 },
    { "gettagforwardvector", CScr_GetTagForwardVector, 0 },
    { "getnormalizedmovement", CScr_GetNormalizedMovement, 0 },
    { "isspectating", CScr_IsSpectatingMethod, 0 },
    { "swimming", CScr_Swimming, 0 },
    { "getlocalclientnumber", CScr_GetLocalClientNumber, 0 },
    { "setblur", CScr_SetBlur, 0 },
    { "linktocamera", CScr_LinkToCamera, 0 },
    { "setshaderconstant", CScr_SetShaderConstant, 0 },
    { "usealternateaimparams", CScr_UseAlternateAimParams, 0 },
    { "clearalternateaimparams", CScr_ClearAlternateAimParams, 0 },
    { "setphysicsgravity", CScr_SetPhysicsGravity, 0 },
    { "clearphysicsgravity", CScr_ClearPhysicsGravity, 0 },
    { nullptr, nullptr, 0 } // end marker, keep last
};
const unsigned int g_csp_client_method_count = ARRAY_COUNT(g_csp_client_methods) - 1;
