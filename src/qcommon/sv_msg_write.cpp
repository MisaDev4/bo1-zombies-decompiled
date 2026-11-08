#include "sv_msg_write.h"
#include <demo/demo_version.h>
#include <server_mp/sv_main_mp.h>
#include "msg.h"
#include <server_mp/sv_snapshot_profile_mp.h>
#include <server_mp/sv_init_mp.h>
#include <cgame/cg_draw_debug.h>
#include <flame/flame_system.h>
#include <qcommon/common.h>

// mod (L51): the entity (and snapshot) MSG_WriteEntityDeltaForEType is writing, so the "too far outside the playable
// area" Com_Errors name it (retail printed only the coordinate). Diagnostic text only, behind no dvar.
static const entityState_s *s_bo1WriteEnt;
static const SnapshotInfo_s *s_bo1WriteSnap;

static const char *MSG_BO1FarEntText(int clientNum)
{
    static char text[1024];
    char desc[768];

    if ( !s_bo1WriteEnt )
    {
        Com_sprintf(text, sizeof(text), " [L51: not an entity delta, client %i]", clientNum);
        return text;
    }
    Com_sprintf(
        text,
        sizeof(text),
        " [L51: ent %i eType %i, %s, sender %s client %i%s]",
        s_bo1WriteEnt->number,
        s_bo1WriteEnt->eType,
        SV_BO1DescribeEnt(s_bo1WriteEnt->number, desc, sizeof(desc)),
        s_bo1WriteSnap && s_bo1WriteSnap->archived ? "archive" : "snapshot",
        clientNum,
        s_bo1WriteSnap && s_bo1WriteSnap->fromBaseline ? " from baseline" : "");
    return text;
}

bool offsetsTested;
const NetFieldList *__cdecl MSG_GetStateFieldListForEntityType(int eType, bool isDemoSnapshot)
{
    int v3; // [esp+0h] [ebp-4h]

    if ( !offsetsTested )
    {
        MSG_CheckForDuplicateOffsets();
        offsetsTested = 1;
    }
    if ( isDemoSnapshot )
        return Demo_GetStateFieldListForEntityType(eType);
    if (eType == ET_SCRIPTMOVER && zombiemode && zombiemode->current.enabled)
    {
        // zombies: SP sends animtreeIndex (es+0xd0, netfield name SP 0x009d8538, 4 bits) in every entity table;
        // the client builds a script model's anim tree from it (CG_GetAnimations SP 0x007766e0). KB appends it
        // to MP's script mover table in zombie mode only, with 7 bits for MAX_XANIMTREE_NUM (128) trees.
        static NetField moverFields[73];
        static const NetFieldList moverList = { moverFields, 73u, "spScriptMoverStateFields" };
        static bool moverInitialized;
        if (!moverInitialized)
        {
            memcpy(moverFields, scriptMoverStateFields, sizeof(scriptMoverStateFields));
            static_assert(sizeof(scriptMoverStateFields) == 72 * sizeof(NetField), "MP script mover table");
            moverFields[72] = { "animtreeIndex", (int)offsetof(entityState_s, animtreeIndex), 1, 7, 0u, "7", "0" };
            MSG_LoopThroughFields(&moverList);
            moverInitialized = true;
        }
        return &moverList;
    }
    if (eType == ET_ACTOR && zombiemode && zombiemode->current.enabled)
    {
        // zombies: actor un3 is mutable and 32 bits (SP netfield 0x00A58AE0).
        // A freed trigger's hintString can remain in the archive baseline when its
        // slot becomes an actor. Preserve the MP table for MP and actor corpses.
        // zombies: SP's actor table (0x00A58838, field 48) also sends animtreeIndex, so the client builds a dog's own
        // tree (SP 0x00454430 -> 0x0060a060); 7 bits as in the mover table above.
        static NetField actorFields[70];
        static const NetFieldList actorList = { actorFields, 70u, "spActorStateFields" };
        static bool initialized;
        if (!initialized)
        {
            memcpy(actorFields, actorStateFields, sizeof(actorStateFields));
            iassert(!strcmp(actorFields[68].name, "un3"));
            actorFields[68].bits = 32;
            actorFields[68].changeHints = 0;
            actorFields[68].bitsStr = "32";
            actorFields[68].changeHintsStr = "0";
            actorFields[69] = { "animtreeIndex", (int)offsetof(entityState_s, animtreeIndex), 1, 7, 0u, "7", "0" };
            MSG_LoopThroughFields(&actorList);
            initialized = true;
        }
        // mod: actorNum is 5 bits for the retail 32 actors; bo1_mod_maxactors > 32 needs 8. Found by name: it is
        // not field 0 (that is eType, MSG_FIELD_ETYPE) - writing index 0 broke every actor delta (msg_mp.cpp 2440).
        static int actorNumField = -1;
        if (actorNumField < 0)
        {
            for (int i = 0; i < 70; ++i)
            {
                if (!strcmp(actorFields[i].name, "lerp.u.actor.actorNum"))
                    actorNumField = i;
            }
            iassert(actorNumField >= 0);
        }
        extern int g_maxActors;
        if (actorNumField >= 0)
        {
            actorFields[actorNumField].bits = g_maxActors <= 32 ? 5 : g_maxActors <= 256 ? 8 : 10; // = G_ActorNumBits()
            actorFields[actorNumField].bitsStr = g_maxActors <= 32 ? "ACTOR_BITS" : g_maxActors <= 256 ? "8" : "10";
        }
        return &actorList;
    }
    if (eType == ET_ACTOR_CORPSE && zombiemode && zombiemode->current.enabled)
    {
        // zombies: an SP actor corpse is the actor entity itself (G_CorpseFromActor SP 0x00642B80) and carries the
        // actor's un3. SP's corpse un3 field (.rdata 0x00A591C0) is 32 bits and still NEVER_CHANGES (hint 1); MP's is
        // a 10-bit submodel. SP's corpse table (field 62) also sends animtreeIndex: a dog corpse keeps its tree.
        static NetField corpseFields[70];
        static const NetFieldList corpseList = { corpseFields, 70u, "spActorCorpseStateFields" };
        static bool corpseInitialized;
        if (!corpseInitialized)
        {
            memcpy(corpseFields, actorStateFields, sizeof(actorStateFields));
            corpseFields[69] = { "animtreeIndex", (int)offsetof(entityState_s, animtreeIndex), 1, 7, 0u, "7", "0" };
            iassert(!strcmp(corpseFields[68].name, "un3"));
            corpseFields[68].bits = 32;
            corpseFields[68].bitsStr = "32";
            MSG_LoopThroughFields(&corpseList);
            corpseInitialized = true;
        }
        return &corpseList;
    }
    if ( eType > 21 )
        v3 = 21;
    else
        v3 = eType;
    return &s_entityNetFieldList[v3];
}

void MSG_CheckForDuplicateOffsets()
{
    unsigned int j; // [esp+0h] [ebp-8h]
    unsigned int i; // [esp+4h] [ebp-4h]

    for ( i = 0; i < 0x16; ++i )
        MSG_LoopThroughFields(&s_entityNetFieldList[i]);
    for ( j = 0; j < 6; ++j )
        MSG_LoopThroughFields(&s_otherNetFieldList[j]);
}

// mod (L25): the GENTITYNUM_BITS rows (retail 10 bits) follow g_entNumBits (11 with bo1_mod_maxactors > 32). G_InitGame
// calls this after setting it, before the zombiemode table copies (moverFields, actorFields, ...) are first made.
void MSG_SetEntityNumBits()
{
    for ( int l = 0; l < 22 + 6; ++l )
    {
        const NetFieldList *list = l < 22 ? &s_entityNetFieldList[l] : &s_otherNetFieldList[l - 22];
        for ( unsigned int i = 0; i < list->count; ++i )
        {
            if ( list->array[i].bitsStr && !strcmp(list->array[i].bitsStr, "GENTITYNUM_BITS") )
                ((NetField *)list->array)[i].bits = g_entNumBits;
        }
    }
}

void __cdecl MSG_LoopThroughFields(const NetFieldList *fieldToCheck)
{
    unsigned int i; // [esp+4h] [ebp-1Ch]
    int jend; // [esp+8h] [ebp-18h]
    int kend; // [esp+Ch] [ebp-14h]
    int kstart; // [esp+10h] [ebp-10h]
    int jstart; // [esp+14h] [ebp-Ch]
    unsigned int k; // [esp+18h] [ebp-8h]
    unsigned int j; // [esp+1Ch] [ebp-4h]

    for ( j = 0; j < fieldToCheck->count - 1; ++j )
    {
        for ( k = j + 1; k < fieldToCheck->count; ++k )
        {
            jstart = fieldToCheck->array[j].offset;
            kstart = fieldToCheck->array[k].offset;
            jend = fieldToCheck->array[j].size + jstart;
            kend = fieldToCheck->array[k].size + kstart;
            if ( (jstart >= kstart || jend > kstart) && (kstart >= jstart || kend > jstart) )
            {
                Com_Printf(
                    15,
                    " Overlapping netfields - EntityField %s : NetField ID#%d - %s (%d:%d)    overlaps to NetField ID#%d - %s (%d:%d)\n",
                    fieldToCheck->fieldArrayName,
                    j,
                    fieldToCheck->array[j].name,
                    jstart,
                    jend,
                    k,
                    fieldToCheck->array[k].name,
                    kstart,
                    kend);
                if ( !Assert_MyHandler(
                                "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                                210,
                                0,
                                "Overlapping net fields") )
                    __debugbreak();
            }
        }
    }
    for ( i = 0; i < fieldToCheck->count; ++i )
    {
        if ( fieldToCheck->array[i].bits > 0
            && fieldToCheck->array[i].bits > 8 * fieldToCheck->array[i].size
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        217,
                        0,
                        "%s",
                        "fieldToCheck->array[i].bits <= 0 || fieldToCheck->array[i].bits <= fieldToCheck->array[i].size*8") )
        {
            __debugbreak();
        }
        if ( (fieldToCheck->array[i].size < 1 || fieldToCheck->array[i].size > 4)
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        218,
                        0,
                        "%s",
                        "fieldToCheck->array[i].size >= 1 && fieldToCheck->array[i].size <= 4") )
        {
            __debugbreak();
        }
    }
}

bool offsetsTested_0;
const NetFieldList *__cdecl MSG_GetNetFieldList(netFieldTypes_t fieldType, bool isDemoSnapshot)
{
    if ( (unsigned int)fieldType >= NET_FIELD_TYPE_COUNT
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    260,
                    0,
                    "%s",
                    "fieldType >= 0 && fieldType < NET_FIELD_TYPE_COUNT") )
    {
        __debugbreak();
    }
    if ( !offsetsTested_0 )
    {
        MSG_CheckForDuplicateOffsets();
        offsetsTested_0 = 1;
    }
    if ( isDemoSnapshot )
        return Demo_GetNetFieldListForType(fieldType);
    if ( fieldType == NET_FIELD_TYPE_CLIENT_STATE && zombiemode && zombiemode->current.enabled )
    {
        // zombies: SP clientState score widths (SP netfield table .rdata 0x00A5C130): score.score is an int
        // delta (type -79, any int; MP: signed 16 bits, hence MP's +-30000 score clamp), score.kills 32 bits
        // (MP 10) and score.headshots 16 bits (MP sends headshots as scoreboardColumns[0] in zombiemode,
        // G_PopulateMatchState; 10 bits). scoreboardColumns[3] carries zombietron's score.scoreMultiplier there
        // (SP 14 bits; g_scr_sp_zombietron.h). Both local endpoints use this table; MP keeps its own.
        static NetField clientFields[50];
        static const NetFieldList clientList = { clientFields, 50u, "spClientStateFields" };
        static bool initialized;
        if ( !initialized )
        {
            memcpy(clientFields, clientStateFields, sizeof(clientFields));
            for ( int i = 0; i < 50; ++i )
            {
                if ( !strcmp(clientFields[i].name, "score.score") || !strcmp(clientFields[i].name, "score.kills") )
                {
                    clientFields[i].bits = 32;
                    clientFields[i].bitsStr = "32";
                }
                else if ( !strcmp(clientFields[i].name, "score.scoreboardColumns[0]") )
                {
                    clientFields[i].bits = 16;
                    clientFields[i].bitsStr = "16";
                }
                else if ( !strcmp(clientFields[i].name, "score.scoreboardColumns[3]") )
                {
                    clientFields[i].bits = 14;
                    clientFields[i].bitsStr = "14";
                }
            }
            initialized = true;
        }
        return &clientList;
    }
    if ( fieldType == NET_FIELD_TYPE_OBJECTIVE && zombiemode && zombiemode->current.enabled )
    {
        // zombies: SP sends an objective's 3D marker in its objective configstring (Objective_Set3D, SP 0x007f7530:
        // keys use3d, col3d, txt3d). The port sends the same values with the objective: use3D (enable bit and
        // UI3D window, 5 bits), color as SP's colour index (3 bits; KB's RGBA colour is unused on SP levels) and
        // the 3D text's CS_LOCALIZED_STRINGS index in the alt_3D_text slot (G_SP_SetObjective3DText).
        static NetField objectiveFieldsSP[11];
        static const NetFieldList objectiveListSP = { objectiveFieldsSP, 11u, "spObjectiveFields" };
        static bool objectiveInitialized;
        if ( !objectiveInitialized )
        {
            int n = 0;
            for ( int i = 0; i < 9; ++i )
            {
                objectiveFieldsSP[n] = objectiveFields[i];
                if ( !strcmp(objectiveFieldsSP[n].name, "color") )
                {
                    objectiveFieldsSP[n].bits = 3;
                    objectiveFieldsSP[n].bitsStr = "3";
                }
                ++n;
            }
            objectiveFieldsSP[n++] = { "use3D", 36, 4, 5, 0u, "5", "0" };
            objectiveFieldsSP[n++] = { "alt_3D_text", 44, 4, 10, 0u, "10", "0" };
            objectiveInitialized = true;
        }
        return &objectiveListSP;
    }
    // zombies (L22): no zombiemode hud elem table. SP sends hud elems through its snapshot with MP's encodings:
    // SP hudElemFields 0x00A5D380 (45 rows = KB's 43 + fontScaleStartTime / fontScaleTime) has fontScale -86
    // (write 0x004DBBFC: 6 bits of round((fs - 1) * 10), wrapping; read 0x0059F096: bits * 0.1 + 1) and
    // color / fromColor / glowColor -85 (5-bit alpha, 0x004DBC37 / 0x0059F0C8). So the real SP client draws
    // Five's fontScale 8 / 16 / 32 as 1.6 / 3.2 / 6.4 (Steam recordings, retail research probe sessions hudc.tsv).
    // L13's full-float / 32-bit override made the popups and the ROUND text 5x too big; MP's table is SP's.
    return &s_otherNetFieldList[fieldType];
}

void __cdecl MSG_WriteReliableCommandToBuffer(
                const char *pszCommand,
                char **pszBuffer,
                char *allocBufferBase,
                int allocBufferLength,
                int *allocBufferPos)
{
    int v5; // [esp+0h] [ebp-20h]
    char *v6; // [esp+14h] [ebp-Ch]
    int iCommandLength; // [esp+18h] [ebp-8h]
    int i; // [esp+1Ch] [ebp-4h]

    iCommandLength = strlen(pszCommand);
    if ( !iCommandLength )
        Com_PrintWarning(16, "WARNING: Empty reliable command\n");
    if ( iCommandLength + *allocBufferPos < allocBufferLength )
        v5 = *allocBufferPos;
    else
        v5 = 0;
    *pszBuffer = &allocBufferBase[v5];
    *allocBufferPos = &(*pszBuffer)[iCommandLength + 1] - allocBufferBase;
    v6 = *pszBuffer;
    for ( i = 0; pszCommand[i]; ++i )
    {
        *v6 = I_CleanChar(pszCommand[i]);
        if ( *v6 == 37 )
            *v6 = 46;
        ++v6;
    }
    (*pszBuffer)[i] = 0;
}

void __cdecl MSG_WriteOriginFloat(
                const SnapshotInfo_s *snapInfo,
                int clientNum,
                msg_t *msg,
                int bits,
                float value,
                float oldValue)
{
    int roundedValue; // [esp+4Ch] [ebp-18h]
    int roundedOldValue; // [esp+54h] [ebp-10h]
    int roundedCenter; // [esp+58h] [ebp-Ch]
    int index; // [esp+5Ch] [ebp-8h]

    if ( msg->readOnly
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 434, 0, "%s", "!msg->readOnly") )
    {
        __debugbreak();
    }
    roundedValue = MSG_RoundFloatToInt(value);
    roundedOldValue = MSG_RoundFloatToInt(oldValue);
    if ( (unsigned int)(roundedValue - roundedOldValue + 64) >= 0x80 )
    {
        MSG_WriteBit1(msg);
        if ( bits == -92 || bits == -66 )
        {
            index = 0;
        }
        else
        {
            if ( bits != -91
                && bits != -65
                && !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                            466,
                            0,
                            "%s",
                            "(bits == MSG_FIELD_ORIGINY) || (bits == MSG_FIELD_ES_ORIGINY)") )
            {
                __debugbreak();
            }
            index = 1;
        }
        if ( !svsHeaderValid
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        470,
                        0,
                        "%s",
                        "svsHeaderValid") )
        {
            __debugbreak();
        }
        roundedCenter = MSG_RoundFloatToInt(svsHeader.mapCenter[index]);
        // zombies: SP MSG_WriteOriginFloat (SP 0x0057E910) writes x/y as 18 bits biased by 0x20000 (+-131072 around the
        // map center), MP as 16 bits biased by 0x8000. Read back by MSG_ReadOriginFloat (SP 0x00898390).
        if ( zombiemode && zombiemode->current.enabled )
            MSG_WriteBits(msg, (roundedOldValue + 0x20000 - roundedCenter) ^ (roundedValue - roundedCenter + 0x20000), 18u);
        else
            MSG_WriteBits(msg, (roundedOldValue + 0x8000 - roundedCenter) ^ (roundedValue - roundedCenter + 0x8000), 0x10u);
    }
    else
    {
        MSG_WriteBit0(msg);
        MSG_WriteBits(msg, roundedValue - roundedOldValue + 64, 7u);
        MSG_PacketAnalyze_WriteOriginFloat_Delta(clientNum, msg, bits, value, roundedValue, roundedOldValue);
    }
}

void __cdecl MSG_PacketAnalyze_WriteOriginFloat_Delta(
                int clientNum,
                msg_t *msg,
                int bits,
                float value,
                int roundedValue,
                int roundedOldValue)
{
    const char *v6; // [esp+38h] [ebp-14h]
    int roundedCenter; // [esp+40h] [ebp-Ch]
    int index; // [esp+44h] [ebp-8h]

    if ( bits == -92 || bits == -66 )
    {
        index = 0;
    }
    else
    {
        if ( bits != -91
            && bits != -65
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        380,
                        0,
                        "%s",
                        "(bits == MSG_FIELD_ORIGINY) || (bits == MSG_FIELD_ES_ORIGINY)") )
        {
            __debugbreak();
        }
        index = 1;
    }
    if ( !svsHeaderValid
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 384, 0, "%s", "svsHeaderValid") )
    {
        __debugbreak();
    }
    roundedCenter = (int)(float)(svsHeader.mapCenter[index] + 0.5);
    // zombies: the SP exe has no such range check and no Com_Error (SP MSG_WriteOriginFloat 0x0057E910 only writes;
    // 18-bit x/y), so an entity past the MP +-32768 box (crash-1249: Y 32810 on Kino) is sent, not a dropped server.
    // BO1_FARENT (SV_BO1CheckFarEntities) still names such an entity once.
    if ( zombiemode && zombiemode->current.enabled )
        return;
    if ( GetMinBitCountForNum((roundedOldValue + 0x8000 - roundedCenter) ^ (roundedValue - roundedCenter + 0x8000)) > 16 )
    {
        if ( index )
            v6 = "Y";
        else
            v6 = "X";
        Com_Error(
            ERR_DROP,
            "Entity with %s coordinate of %f is too far outside the playable area of the map.    The playable area goes from ( %f"
            ", %f, %f ) to ( %f, %f, %f )%s\n",
            v6,
            value,
            (float)(svsHeader.mapCenter[0] - 32768.0),
            (float)(svsHeader.mapCenter[1] - 32768.0),
            (float)(svsHeader.mapCenter[2] - 65536.0),
            (float)((float)(svsHeader.mapCenter[0] + 32768.0) - 1.0),
            (float)((float)(svsHeader.mapCenter[1] + 32768.0) - 1.0),
            (float)((float)(svsHeader.mapCenter[2] + 65536.0) - 1.0),
            MSG_BO1FarEntText(clientNum)); // mod (L51): name the entity
    }
}

void __cdecl MSG_WriteOriginZFloat(
                const SnapshotInfo_s *snapInfo,
                int clientNum,
                msg_t *msg,
                float value,
                float oldValue)
{
    int roundedValue; // [esp+48h] [ebp-14h]
    int roundedOldValue; // [esp+50h] [ebp-Ch]
    int roundedCenter; // [esp+54h] [ebp-8h]

    if ( msg->readOnly
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 543, 0, "%s", "!msg->readOnly") )
    {
        __debugbreak();
    }
    roundedValue = MSG_RoundFloatToInt(value);
    roundedOldValue = MSG_RoundFloatToInt(oldValue);
    if ( (unsigned int)(roundedValue - roundedOldValue + 64) >= 0x80 )
    {
        MSG_WriteBit1(msg);
        if ( !svsHeaderValid
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        567,
                        0,
                        "%s",
                        "svsHeaderValid") )
        {
            __debugbreak();
        }
        roundedCenter = MSG_RoundFloatToInt(svsHeader.mapCenter[2]);
        MSG_WriteBits(msg, (roundedOldValue + 0x10000 - roundedCenter) ^ (roundedValue - roundedCenter + 0x10000), 0x11u);
    }
    else
    {
        MSG_WriteBit0(msg);
        MSG_PacketAnalyze_WriteOriginZFloat_Delta(snapInfo, clientNum, msg, value, roundedValue, roundedOldValue);
        MSG_WriteBits(msg, roundedValue - roundedOldValue + 64, 7u);
    }
}

void __cdecl MSG_PacketAnalyze_WriteOriginZFloat_Delta(
                const SnapshotInfo_s *snapInfo,
                int clientNum,
                msg_t *msg,
                float value,
                int roundedValue,
                int roundedOldValue)
{
    if ( !svsHeaderValid
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 494, 0, "%s", "svsHeaderValid") )
    {
        __debugbreak();
    }
    // zombies: no range check or Com_Error in SP MSG_WriteOriginZFloat (SP 0x0061C150; 17 bits as MP).
    if ( zombiemode && zombiemode->current.enabled )
        return;
    if ( GetMinBitCountForNum(
                 (roundedOldValue + 0x10000 - (int)(float)(svsHeader.mapCenter[2] + 0.5))
             ^ (roundedValue - (int)(float)(svsHeader.mapCenter[2] + 0.5) + 0x10000)) > 17 )
        Com_Error(
            ERR_DROP,
            "Entity with Z coordinate of %f is too far outside the playable area of the map.    The playable area goes from ( %f,"
            " %f, %f ) to ( %f, %f, %f )%s\n",
            value,
            (float)(svsHeader.mapCenter[0] - 32768.0),
            (float)(svsHeader.mapCenter[1] - 32768.0),
            (float)(svsHeader.mapCenter[2] - 65536.0),
            (float)((float)(svsHeader.mapCenter[0] + 32768.0) - 1.0),
            (float)((float)(svsHeader.mapCenter[1] + 32768.0) - 1.0),
            (float)((float)(svsHeader.mapCenter[2] + 65536.0) - 1.0),
            MSG_BO1FarEntText(clientNum)); // mod (L51): name the entity
}

void __cdecl MSG_WriteDemoRoundedFloat(
                msg_t *msg,
                int bits,
                float value,
                float oldValue,
                unsigned int diffBits,
                int fullBits)
{
    int bitsNeeded; // [esp+2Ch] [ebp-24h]
    int roundedValue; // [esp+30h] [ebp-20h]
    unsigned int roundedValuea; // [esp+30h] [ebp-20h]
    int fullBias; // [esp+34h] [ebp-1Ch]
    int truncDelta; // [esp+38h] [ebp-18h]
    int roundedOldValue; // [esp+3Ch] [ebp-14h]
    int roundedCenter; // [esp+40h] [ebp-10h]
    int index; // [esp+44h] [ebp-Ch]
    int diffBias; // [esp+48h] [ebp-8h]

    if ( msg->readOnly
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 595, 0, "%s", "!msg->readOnly") )
    {
        __debugbreak();
    }
    diffBias = 1 << (diffBits - 1);
    fullBias = 1 << (fullBits - 1);
    roundedValue = MSG_RoundFloatToInt(value);
    roundedOldValue = MSG_RoundFloatToInt(oldValue);
    truncDelta = roundedValue - roundedOldValue;
    if ( diffBias + roundedValue - roundedOldValue < 0 || diffBias + truncDelta >= 1 << diffBits )
    {
        MSG_WriteBit1(msg);
        if ( bits == -103 || bits == -102 || bits == -101 )
        {
            if ( bits == -103 )
            {
                index = 0;
            }
            else if ( bits == -102 )
            {
                index = 1;
            }
            else
            {
                index = 2;
            }
            if ( !svsHeaderValid
                && !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                            625,
                            0,
                            "%s",
                            "svsHeaderValid") )
            {
                __debugbreak();
            }
            roundedCenter = (int)(float)(svsHeader.mapCenter[index] + 0.5);
            roundedValuea = (roundedOldValue + fullBias - roundedCenter) ^ (fullBias + roundedValue - roundedCenter);
        }
        else
        {
            roundedValuea = (fullBias + roundedOldValue) ^ (fullBias + roundedValue);
        }
        bitsNeeded = GetMinBitCountForNum(roundedValuea);
        if ( bitsNeeded > fullBits )
            Com_PrintError(
                14,
                "Demo Error: Bits needed to transmit %d in field %d is %d but fullBits is only %d\n",
                roundedValuea,
                bits,
                bitsNeeded,
                fullBits);
        MSG_WriteBits(msg, roundedValuea, fullBits);
    }
    else
    {
        MSG_WriteBit0(msg);
        MSG_WriteBits(msg, diffBias + truncDelta, diffBits);
    }
}

bool __cdecl MSG_ValuesAreEqual(int clientNum, int bits, int size, const int *fromF, const int *toF)
{
    bool result; // al
    bool v6; // [esp+20h] [ebp-74h]

    if ( size == 1 )
    {
        if ( *(char *)fromF == *(char *)toF )
            return 1;
    }
    else if ( size == 2 )
    {
        if ( *(__int16 *)fromF == *(__int16 *)toF )
            return 1;
    }
    else if ( size == 4 && *fromF == *toF )
    {
        return 1;
    }
    switch ( bits )
    {
        case -107:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, 0.0, 60.0, 100, 0) == 0;
            break;
        case -106:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, 0.0, 255.0, 100, 0) == 0;
            break;
        case -105:
            result = ((int)(float)((float)(*(float *)fromF * 1024.0) / 360.0) & 0x3FF) == ((int)(float)((float)(*(float *)toF * 1024.0)
                                                                                                                                                                                                / 360.0)
                                                                                                                                                                     & 0x3FF);
            break;
        case -104:
        case -103:
        case -102:
        case -101:
        case -92:
        case -91:
        case -90:
        case -66:
        case -65:
        case -64:
            result = MSG_RoundFloatToInt(*(float *)fromF) == MSG_RoundFloatToInt(*(float *)toF);
            break;
        case -100:
        case -87:
        case -62:
            result = (unsigned __int16)(int)(float)(*(float *)fromF * 182.04445) == (unsigned __int16)(int)(float)(*(float *)toF * 182.04445);
            break;
        case -98:
            result = ((*toF ^ *fromF) & 0xFFFFFF) == 0;
            break;
        case -95:
            result = *fromF / 100 == *toF / 100;
            break;
        case -86:
            result = MSG_RoundFloatToInt((float)(*(float *)fromF - 1.0) * 10.0f) == MSG_RoundFloatToInt((float)(*(float *)toF - 1.0) * 10.0f);
            break;
        case -85:
            v6 = *(unsigned __int8 *)fromF == *(unsigned __int8 *)toF
                && *((unsigned __int8 *)fromF + 1) == *((unsigned __int8 *)toF + 1)
                && *((unsigned __int8 *)fromF + 2) == *((unsigned __int8 *)toF + 2)
                && (int)*((unsigned __int8 *)fromF + 3) >> 3 == (int)*((unsigned __int8 *)toF + 3) >> 3;
            result = v6;
            break;
        case -84:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, 0.0, 1.0, 1000, 1) == 0;
            break;
        case -83:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, 0.0, 1.5, 10, 0) == 0;
            break;
        case -82:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, 0.0, 3.0, 100, 1) == 0;
            break;
        case -81:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, 0.0, 1.0, 100, 0) == 0;
            break;
        case -80:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, 0.0, 1.0, 1000, 0) == 0;
            break;
        case -77:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, -1.0, 1.0, 100, 0) == 0;
            break;
        case -76:
            result = MSG_WriteRangedFloat(
                                 clientNum,
                                 0,
                                 (float)*(__int16 *)fromF * 0.000030517578,
                                 (float)*(__int16 *)toF * 0.000030517578,
                                 -1.0,
                                 1.0,
                                 10,
                                 0) == 0;
            break;
        case -75:
            result = MSG_WriteRangedFloat(
                                 clientNum,
                                 0,
                                 (float)*(__int16 *)fromF * 0.0054931641,
                                 (float)*(__int16 *)toF * 0.0054931641,
                                 -90.0,
                                 90.0,
                                 10,
                                 0) == 0;
            break;
        case -74:
            result = MSG_WriteRangedFloat(
                                 clientNum,
                                 0,
                                 (float)*(__int16 *)fromF * 0.0054931641,
                                 (float)*(__int16 *)toF * 0.0054931641,
                                 -180.0,
                                 180.0,
                                 10,
                                 0) == 0;
            break;
        case -73:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, -12.0, 12.0, 10, 0) == 0;
            break;
        case -72:
            result = MSG_WriteRangedFloat(clientNum, 0, *(float *)fromF, *(float *)toF, -6000.0, 6000.0, 10, 0) == 0;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

void __cdecl MSG_WriteEntityIndex(const SnapshotInfo_s *snapInfo, msg_t *msg, int index, int indexBits)
{
    // kcod4
    iassert( !msg->readOnly );

    // in blops, these are stubbed out and outlined, could be a BO1TODO
    //if (msg_printEntityNums->current.enabled && SV_IsPacketDataNetworkData())
    //    Com_Printf(15, "Writing entity num %i\n", index);

    iassert(index - msg->lastEntityRef > 0);

    if (index - msg->lastEntityRef == 1)
    {
        //if (msg_printEntityNums->current.enabled && SV_IsPacketDataNetworkData())
        //    Com_Printf(16, "Wrote entity num: 1 bit (inc)\n");
        MSG_WriteBit1(msg);
    }
    else
    {
        MSG_WriteBit0(msg);
        if ((indexBits == 10 || indexBits == g_entNumBits) && index - msg->lastEntityRef < 16) // mod (L25): networked entity numbers 1537+
        {
            //if (msg_printEntityNums->current.enabled && SV_IsPacketDataNetworkData())
            //    Com_Printf(16, "Wrote entity num: %i bits (delta)\n", 6);
            
            iassert(index - msg->lastEntityRef > 0);

            MSG_WriteBit0(msg);
            MSG_WriteBits(msg, index - msg->lastEntityRef, 4u);
        }
        else
        {
            //if (msg_printEntityNums->current.enabled && SV_IsPacketDataNetworkData())
            //    Com_Printf(16, "Wrote entity num: %i bits (full)\n", indexBits + 2);

            if (indexBits == 10 || indexBits == g_entNumBits) // mod (L25): networked entity numbers 1537+
                MSG_WriteBit1(msg);

            MSG_WriteBits(msg, index, indexBits);
        }
    }
    msg->lastEntityRef = index;
}

void __cdecl MSG_WriteLastChangedField(msg_t *msg, int lastChangedFieldNum, int numFields)
{
    unsigned int idealBits; // [esp+0h] [ebp-4h]

    iassert(!msg->readOnly);
    iassert(lastChangedFieldNum <= numFields);
    idealBits = GetMinBitCountForNum(numFields);
    MSG_WriteBits(msg, lastChangedFieldNum, idealBits);
}

void __cdecl MSG_WriteEventNum(int clientNum, msg_t *msg, unsigned __int8 eventNum)
{
    iassert(!msg->readOnly);
    MSG_WriteByte(msg, eventNum);
}

void __cdecl MSG_WriteEventParam(int clientNum, msg_t *msg, int eventParam)
{
    if ( msg->readOnly
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    1045,
                    0,
                    "%s",
                    "!msg->readOnly") )
    {
        __debugbreak();
    }
    MSG_WriteBits(msg, eventParam, 0xBu);
}

int __cdecl MSG_HighBitNumber(unsigned int v)
{
    unsigned int i; // [esp+0h] [ebp-4h]
    unsigned int va; // [esp+Ch] [ebp+8h]

    iassert(v);
    i = (v & 0xFFFF0000) != 0 ? 0x10 : 0;
    va = v >> ((v & 0xFFFF0000) != 0 ? 0x10 : 0);
    if ( (va & 0xFF00) != 0 )
    {
        i |= 8u;
        va >>= 8;
    }
    if ( (va & 0xF0) != 0 )
    {
        i |= 4u;
        va >>= 4;
    }
    if ( (va & 0xC) != 0 )
    {
        i |= 2u;
        va >>= 2;
    }
    return i | (va >> 1);
}

int __cdecl MSG_WriteRangedFloat(
                int clientNum,
                msg_t *msg,
                float oldValue,
                float value,
                float begin,
                float end,
                int precision,
                bool oftenZeroOrOne)
{
    float v9; // [esp+10h] [ebp-A0h]
    float v10; // [esp+14h] [ebp-9Ch]
    float v11; // [esp+28h] [ebp-88h]
    float v12; // [esp+2Ch] [ebp-84h]
    float v13; // [esp+50h] [ebp-60h]
    float v14; // [esp+54h] [ebp-5Ch]
    float v15; // [esp+68h] [ebp-48h]
    float v16; // [esp+6Ch] [ebp-44h]
    float v17; // [esp+70h] [ebp-40h]
    float v18; // [esp+74h] [ebp-3Ch]
    unsigned int rangeSize; // [esp+88h] [ebp-28h]
    float newVal; // [esp+90h] [ebp-20h]
    unsigned int iNewValue; // [esp+94h] [ebp-1Ch]
    int rangeBits; // [esp+98h] [ebp-18h]
    unsigned int xorValue; // [esp+9Ch] [ebp-14h]
    float oldVal; // [esp+A4h] [ebp-Ch]
    int v; // [esp+A8h] [ebp-8h]
    float range; // [esp+ACh] [ebp-4h]

    if ( msg
        && msg->readOnly
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    1085,
                    0,
                    "%s",
                    "!msg || !msg->readOnly") )
    {
        __debugbreak();
    }
    if ( end < begin
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    1086,
                    0,
                    "%s\n\t%s",
                    "end >= begin",
                    "MSG_WriteRangedFloat, end of range is less then the begining.") )
    {
        __debugbreak();
    }
    if ( precision <= 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    1087,
                    0,
                    "%s\n\t%s",
                    "precision > 0",
                    "MSG_WriteRangedFloat, precision must be positive.") )
    {
        __debugbreak();
    }
    range = (float)(end - begin) * (float)precision;
    if ( range <= 4294967300.0 )
    {
        rangeSize = (__int64)range;
        rangeBits = 0;
        if ( rangeSize )
        {
            rangeBits = GetMinBitCountForNum(rangeSize);
            if ( MSG_HighBitNumber(rangeSize) + 1 != rangeBits
                && !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                            1123,
                            0,
                            "%s",
                            "(MSG_HighBitNumber(rangeSize) + 1) == rangeBits") )
            {
                __debugbreak();
            }
        }
        if ( (float)(value - end) < 0.0 )
            v18 = value;
        else
            v18 = end;
        if ( (float)(begin - value) < 0.0 )
            v14 = v18;
        else
            v14 = begin;
        newVal = (float)((float)(v14 - begin) * (float)precision) + 0.5;
        if ( newVal > range )
            newVal = (float)(end - begin) * (float)precision;
        if ( (float)(oldValue - end) < 0.0 )
            v17 = oldValue;
        else
            v17 = end;
        if ( (float)(begin - oldValue) < 0.0 )
            v13 = v17;
        else
            v13 = begin;
        oldVal = (float)((float)(v13 - begin) * (float)precision) + 0.5;
        if ( oldVal > range )
            oldVal = (float)(end - begin) * (float)precision;
        iNewValue = (__int64)newVal;
        xorValue = iNewValue ^ (__int64)oldVal;
        if ( msg )
        {
            if ( !xorValue )
                Com_PrintWarning(15, "MSG_WriteRangedFloat writing %d bits of 0.\n", rangeBits);
            if ( oftenZeroOrOne )
            {
                if ( (float)(0.0 - end) < 0.0 )
                    v16 = 0.0f;
                else
                    v16 = end;
                if ( (float)(begin - 0.0) < 0.0 )
                    v12 = v16;
                else
                    v12 = begin;
                if ( (float)((float)((float)(v12 - begin) / (float)precision) + 0.5) <= range )
                    v11 = (float)((float)(v12 - begin) / (float)precision) + 0.5;
                else
                    v11 = (float)(end - begin) * (float)precision;
                if ( (float)(1.0 - end) < 0.0 )
                    v15 = 1.0f;
                else
                    v15 = end;
                if ( (float)(begin - 1.0) < 0.0 )
                    v10 = v15;
                else
                    v10 = begin;
                if ( (float)((float)((float)(v10 - begin) / (float)precision) + 0.5) <= range )
                    v9 = (float)((float)(v10 - begin) / (float)precision) + 0.5;
                else
                    v9 = (float)(end - begin) * (float)precision;
                if ( iNewValue == (__int64)v11 )
                {
                    MSG_WriteBits(msg, 1, 2u);
                }
                else if ( iNewValue == (__int64)v9 )
                {
                    MSG_WriteBits(msg, 3, 2u);
                }
                else
                {
                    MSG_WriteBit0(msg);
                    MSG_WriteBits(msg, xorValue, rangeBits);
                }
            }
            else
            {
                MSG_WriteBits(msg, xorValue, rangeBits);
            }
        }
        return iNewValue ^ (__int64)oldVal;
    }
    else
    {
        Com_PrintWarning(
            15,
            "The numerical space defined by range/precision combination is too large. No compression performed.\n");
        v = LODWORD(oldValue) ^ LODWORD(value);
        if ( msg )
        {
            if ( !v )
                Com_PrintWarning(15, "MSG_WriteRangedFloat writing 0.\n");
            MSG_WriteLong(msg, v);
        }
        return LODWORD(oldValue) ^ LODWORD(value);
    }
}

PacketEntityType __cdecl MSG_GetPacketEntityTypeForEType(int eType)
{
    PacketEntityType result; // eax

    switch ( eType )
    {
        case 0:
            result = ANALYZE_DATATYPE_ENTITYTYPE_GENERALENTITY;
            break;
        case 1:
            result = ANALYZE_DATATYPE_ENTITYTYPE_PLAYERENTITY;
            break;
        case 2:
            result = ANALYZE_DATATYPE_ENTITYTYPE_PLAYERCORPSEENTITY;
            break;
        case 3:
            result = ANALYZE_DATATYPE_ENTITYTYPE_ITEMENTITY;
            break;
        case 4:
            result = ANALYZE_DATATYPE_ENTITYTYPE_MISSILEENTITY;
            break;
        case 5:
            result = ANALYZE_DATATYPE_ENTITYTYPE_INVISIBLEENTITY;
            break;
        case 6:
            result = ANALYZE_DATATYPE_ENTITYTYPE_SCRIPTMOVERENTITY;
            break;
        case 7:
            result = ANALYZE_DATATYPE_ENTITYTYPE_SOUNDBLENDENTITY;
            break;
        case 8:
            result = ANALYZE_DATATYPE_ENTITYTYPE_FXENTITY;
            break;
        case 9:
            result = ANALYZE_DATATYPE_ENTITYTYPE_LOOPFXENTITY;
            break;
        case 11:
            result = ANALYZE_DATATYPE_ENTITYTYPE_MG42ENTITY;
            break;
        case 12:
            result = ANALYZE_DATATYPE_ENTITYTYPE_HELICOPTER;
            break;
        case 13:
            result = ANALYZE_DATATYPE_ENTITYTYPE_PLANE;
            break;
        case 14:
            result = ANALYZE_DATATYPE_ENTITYTYPE_VEHICLE;
            break;
        case 15:
            result = ANALYZE_DATATYPE_ENTITYTYPE_VEHICLE_COLLMAP;
            break;
        // Identify actors and corpses in the netfield diagnostic.
        // Brief said a temp entity changes un3; the trace identifies ET_ACTOR.
        case ET_VEHICLE_CORPSE:
            result = ANALYZE_DATATYPE_ENTITYTYPE_VEHICLE_CORPSE;
            break;
        case ET_ACTOR:
            result = ANALYZE_DATATYPE_ENTITYTYPE_ACTOR;
            break;
        case ET_ACTOR_SPAWNER:
            result = ANALYZE_DATATYPE_ENTITYTYPE_ACTOR_SPAWNER;
            break;
        case ET_ACTOR_CORPSE:
            result = ANALYZE_DATATYPE_ENTITYTYPE_ACTOR_CORPSE;
            break;
        case 20:
            result = ANALYZE_DATATYPE_ENTITYTYPE_STREAMER_HINT;
            break;
        default:
            result = ANALYZE_DATATYPE_ENTITYTYPE_TEMPENTITY;
            break;
    }
    return result;
}

void __cdecl MSG_WriteValue(
                const SnapshotInfo_s *snapInfo,
                msg_t *msg,
                const int *fromF,
                const int *toF,
                int bits,
                int size)
{
    int fromVal; // [esp+0h] [ebp-8h]
    int value; // [esp+4h] [ebp-4h]

    fromVal = MSG_GetField(fromF, size);
    value = fromVal ^ MSG_GetField(toF, size);
    if ( bits != 32 )
        value &= (1 << bits) - 1;
    MSG_WriteValueNoXor(snapInfo, msg, value, bits);
}

void __cdecl MSG_WriteValueNoXor(const SnapshotInfo_s *snapInfo, msg_t *msg, int value, int bits)
{
    int absbits; // [esp+0h] [ebp-8h]
    unsigned int partialBits; // [esp+4h] [ebp-4h]

    absbits = abs(bits);
    partialBits = absbits & 7;
    if ( (absbits & 7) != 0 )
    {
        MSG_WriteBits(msg, value, partialBits);
        absbits -= partialBits;
        value >>= partialBits;
    }
    while ( absbits )
    {
        MSG_WriteByte(msg, value);
        value >>= 8;
        absbits -= 8;
    }
}

char __cdecl MSG_WriteDeltaField_Internal(
                const SnapshotInfo_s *snapInfo,
                msg_t *msg,
                int time,
                const unsigned __int8 *from,
                const unsigned __int8 *to,
                const NetField *field,
                int fieldNum,
                const int *fromF,
                const int *toF)
{
    int v9; // eax
    unsigned __int8 v10; // al
    int v11; // eax
    int v12; // eax
    int v13; // esi
    unsigned int MinBitCountForNum; // eax
    int delta; // [esp+5Ch] [ebp-28h]
    int diff; // [esp+60h] [ebp-24h]
    int trunc; // [esp+6Ch] [ebp-18h]
    int trunca; // [esp+6Ch] [ebp-18h]
    int truncb; // [esp+6Ch] [ebp-18h]
    int truncd; // [esp+6Ch] [ebp-18h]
    int truncc; // [esp+6Ch] [ebp-18h]
    int trunce; // [esp+6Ch] [ebp-18h]
    int oldTrunc; // [esp+70h] [ebp-14h]
    int oldTrunca; // [esp+70h] [ebp-14h]
    int oldTruncb; // [esp+70h] [ebp-14h]
    float fullFloat; // [esp+78h] [ebp-Ch]
    float fullFloata; // [esp+78h] [ebp-Ch]
    int value; // [esp+80h] [ebp-4h]

    switch ( field->bits )
    {
        case 0xFFFFFF95:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, 0.0, 60.0, 100, 0);
            break;
        case 0xFFFFFF96:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, 0.0, 255.0, 100, 0);
            break;
        case 0xFFFFFF97:
            MSG_WriteAngle(msg, 0xAu, *(float *)toF);
            break;
        case 0xFFFFFF98:
            MSG_WriteDemoRoundedFloat(msg, field->bits, *(float *)toF, *(float *)fromF, 7u, 12);
            break;
        case 0xFFFFFF99:
        case 0xFFFFFF9A:
        case 0xFFFFFF9B:
            MSG_WriteDemoRoundedFloat(msg, field->bits, *(float *)toF, *(float *)fromF, 5u, 16);
            break;
        case 0xFFFFFF9C:
        case 0xFFFFFFC2:
            MSG_WriteDeltaAngle(snapInfo, msg, *(float *)fromF, *(float *)toF);
            break;
        case 0xFFFFFF9D:
            fullFloata = *(float *)toF;
            truncc = (int)*(float *)toF;
            oldTruncb = (int)*(float *)fromF;
            if ( *(float *)toF != 0.0 || LODWORD(fullFloata) == 0x80000000 )
            {
                MSG_WriteBit1(msg);
                if ( LODWORD(fullFloata) == 0x80000000 || (float)truncc != fullFloata || (unsigned int)(truncc + 2048) >= 0x1000 )
                {
                    MSG_WriteBit1(msg);
                    MSG_WriteLong(msg, *fromF ^ *toF);
                }
                else
                {
                    MSG_WriteBit0(msg);
                    trunce = (truncc + 2048) ^ (oldTruncb + 2048);
                    MSG_WriteBits(msg, trunce, 4u);
                    MSG_WriteByte(msg, trunce >> 4);
                }
            }
            else
            {
                MSG_WriteBit0(msg);
            }
            if ( (unsigned int)(__int64)(*(float *)toF + 2048.0) >= 0x1000
                && !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                            1841,
                            0,
                            "*(float *)toF + HUDELEM_COORD_BIAS doesn't index 1 << HUDELEM_COORD_BITS\n\t%i not in [0, %i)",
                            (int)(float)(*(float *)toF + 2048.0),
                            4096) )
            {
                __debugbreak();
            }
            break;
        case 0xFFFFFF9E:
            MSG_WriteEFlags(snapInfo->clientNum, msg, *fromF, *toF);
            break;
        case 0xFFFFFF9F:
        case 0xFFFFFFB9:
        case 0xFFFFFFBA:
        case 0xFFFFFFBC:
            MSG_WriteDeltaTime(snapInfo->clientNum, msg, time, *toF);
            break;
        case 0xFFFFFFA0:
            v9 = MSG_GetField(toF, field->size);
            MSG_WriteGroundEntityNum(snapInfo->clientNum, msg, v9);
            break;
        case 0xFFFFFFA1:
            MSG_WriteBits(msg, *toF / 100, 7u);
            break;
        case 0xFFFFFFA2:
            v10 = MSG_GetField(toF, field->size);
            MSG_WriteEventNum(snapInfo->clientNum, msg, v10);
            break;
        case 0xFFFFFFA3:
            v11 = MSG_GetField(toF, field->size);
            MSG_WriteEventParam(snapInfo->clientNum, msg, v11);
            break;
        case 0xFFFFFFA4:
        case 0xFFFFFFA5:
        case 0xFFFFFFBE:
        case 0xFFFFFFBF:
            MSG_WriteOriginFloat(snapInfo, snapInfo->clientNum, msg, field->bits, *(float *)toF, *(float *)fromF);
            break;
        case 0xFFFFFFA6:
        case 0xFFFFFFC0:
            MSG_WriteOriginZFloat(snapInfo, snapInfo->clientNum, msg, *(float *)toF, *(float *)fromF);
            break;
        case 0xFFFFFFA7:
            truncb = (int)*(float *)toF;
            oldTrunca = (int)*(float *)fromF;
            if ( (float)truncb != *(float *)toF || *toF == 0x80000000 || (unsigned int)(truncb + 4096) >= 0x2000 )
                goto LABEL_19;
            MSG_WriteBit0(msg);
            truncd = (truncb + 4096) ^ (oldTrunca + 4096);
            MSG_WriteBits(msg, truncd, 5u);
            MSG_WriteByte(msg, truncd >> 5);
            break;
        case 0xFFFFFFA8:
            MSG_WriteLong(msg, *fromF ^ *toF);
            break;
        case 0xFFFFFFA9:
            MSG_WriteAngle16(msg, *(float *)toF);
            break;
        case 0xFFFFFFAA:
            MSG_WriteBits(msg, MSG_RoundFloatToInt((float)(*(float *)toF - 1.0) * 10.0f), 6u);
            break;
        case 0xFFFFFFAB:
            if ( (*((unsigned __int8 *)fromF + 3) == 255 && !*((_BYTE *)toF + 3)
                 || !*((_BYTE *)fromF + 3) && *((unsigned __int8 *)toF + 3) == 255)
                && !memcmp(fromF, toF, 3u) )
            {
                goto LABEL_4;
            }
            MSG_WriteBit0(msg);
            if ( *(unsigned __int8 *)fromF == *(unsigned __int8 *)toF
                && *((unsigned __int8 *)fromF + 1) == *((unsigned __int8 *)toF + 1)
                && *((unsigned __int8 *)fromF + 2) == *((unsigned __int8 *)toF + 2) )
            {
                MSG_WriteBit1(msg);
            }
            else
            {
                MSG_WriteBit0(msg);
                MSG_WriteByte(msg, *(_BYTE *)toF);
                MSG_WriteByte(msg, *((_BYTE *)toF + 1));
                MSG_WriteByte(msg, *((_BYTE *)toF + 2));
            }
            MSG_WriteBits(msg, (int)*((unsigned __int8 *)toF + 3) >> 3, 5u);
            break;
        case 0xFFFFFFAC:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, 0.0, 1.0, 1000, 1);
            break;
        case 0xFFFFFFAD:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, 0.0, 1.5, 10, 0);
            break;
        case 0xFFFFFFAE:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, 0.0, 3.0, 100, 1);
            break;
        case 0xFFFFFFAF:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, 0.0, 1.0, 100, 0);
            break;
        case 0xFFFFFFB0:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, 0.0, 1.0, 1000, 0);
            break;
        case 0xFFFFFFB1:
            value = *toF - *fromF;
            if ( value >= 64 || value < -64 )
            {
                if ( value >= 4096 || value < -4096 )
                {
                    if (value >= 0x800000 || value < (int)0xFF800000)
                    {
                        MSG_WriteBits(msg, 3, 2u);
                        MSG_WriteLong(msg, value);
                    }
                    else
                    {
                        MSG_WriteBits(msg, 2, 2u);
                        MSG_WriteByte(msg, value);
                        MSG_WriteShort(msg, value >> 8);
                    }
                }
                else
                {
                    MSG_WriteBits(msg, 1, 2u);
                    MSG_WriteBits(msg, value, 0xDu);
                }
            }
            else
            {
                MSG_WriteBits(msg, 0, 2u);
                MSG_WriteBits(msg, value, 7u);
            }
            break;
        case 0xFFFFFFB2:
            MSG_WriteDeltaFrameTime(snapInfo->clientNum, msg, time, *toF);
            break;
        case 0xFFFFFFB3:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, -1.0, 1.0, 100, 0);
            break;
        case 0xFFFFFFB4:
            MSG_WriteRangedFloat(
                snapInfo->clientNum,
                msg,
                (float)*(__int16 *)fromF * 0.000030517578,
                (float)*(__int16 *)toF * 0.000030517578,
                -1.0,
                1.0,
                10,
                0);
            break;
        case 0xFFFFFFB5:
            MSG_WriteRangedFloat(
                snapInfo->clientNum,
                msg,
                (float)*(__int16 *)fromF * 0.0054931641,
                (float)*(__int16 *)toF * 0.0054931641,
                -90.0,
                90.0,
                10,
                0);
            break;
        case 0xFFFFFFB6:
            MSG_WriteRangedFloat(
                snapInfo->clientNum,
                msg,
                (float)*(__int16 *)fromF * 0.0054931641,
                (float)*(__int16 *)toF * 0.0054931641,
                -180.0,
                180.0,
                10,
                0);
            break;
        case 0xFFFFFFB7:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, -12.0, 12.0, 10, 0);
            break;
        case 0xFFFFFFB8:
            MSG_WriteRangedFloat(snapInfo->clientNum, msg, *(float *)fromF, *(float *)toF, -6000.0, 6000.0, 10, 0);
            break;
        case 0xFFFFFFBB:
        case 0xFFFFFFBD:
        case 0xFFFFFFC1:
        case 0xFFFFFFC3:
            MSG_WriteLong(msg, *toF);
            break;
        case 0xFFFFFFC4:
            delta = *toF - *fromF;
            if ( (int)abs(delta) >= 16 )
            {
                MSG_WriteBit0(msg);
                MSG_WriteValue(snapInfo, msg, fromF, toF, -8, field->size);
            }
            else
            {
                MSG_WriteBit1(msg);
                MSG_WriteBits(msg, delta + 16, 5u);
            }
            break;
        case 0xFFFFFFC5:
            v13 = MSG_GetField(toF, field->size);
            diff = v13 - MSG_GetField(fromF, field->size);
            if ( diff < 1 || diff > 16 )
            {
                MSG_WriteBit0(msg);
                MSG_WriteValue(snapInfo, msg, fromF, toF, 8, field->size);
            }
            else
            {
                MSG_WriteBit1(msg);
                MinBitCountForNum = GetMinBitCountForNum(0x10u);
                MSG_WriteBits(msg, diff - 1, MinBitCountForNum);
            }
            break;
        case 0xFFFFFFC6:
            v12 = MSG_GetField(toF, field->size);
            MSG_WriteValueNoXor(snapInfo, msg, v12, 8);
            break;
        case 0:
            fullFloat = *(float *)toF;
            trunc = (int)*(float *)toF;
            oldTrunc = (int)*(float *)fromF;
            if ( *(float *)toF == 0.0 )
            {
                MSG_WriteBit0(msg);
                if ( LODWORD(fullFloat) == 0x80000000 )
LABEL_4:
                    MSG_WriteBit1(msg);
                else
                    MSG_WriteBit0(msg);
            }
            else
            {
                MSG_WriteBit1(msg);
                if ( LODWORD(fullFloat) == 0x80000000
                    || (float)trunc != fullFloat
                    || (unsigned int)(trunc + 4096) >= 0x2000
                    || (unsigned int)(oldTrunc + 4096) >= 0x2000 )
                {
LABEL_19:
                    MSG_WriteBit1(msg);
                    MSG_WriteLong(msg, *fromF ^ *toF);
                }
                else
                {
                    MSG_WriteBit0(msg);
                    trunca = (trunc + 4096) ^ (oldTrunc + 4096);
                    if ( (unsigned int)trunca >= 0x2000
                        && !Assert_MyHandler(
                                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                                    1737,
                                    0,
                                    "trunc not in [0, (1 << FLOAT_INT_BITS) - 1]\n\t%i not in [%i, %i]",
                                    trunca,
                                    0,
                                    0x1FFF) )
                    {
                        __debugbreak();
                    }
                    MSG_WriteBits(msg, trunca, 5u);
                    MSG_WriteByte(msg, trunca >> 5);
                }
            }
            break;
        default:
            MSG_WriteDeltaField_Default(snapInfo, msg, field, fromF, toF);
            break;
    }
    return 1;
}

void __cdecl MSG_WriteDeltaTime(int clientNum, msg_t *msg, int timeBase, int time)
{
    if ( msg->readOnly
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 923, 0, "%s", "!msg->readOnly") )
    {
        __debugbreak();
    }
    if ( time - timeBase > 0 || time - timeBase <= -256 )
    {
        MSG_WriteBit1(msg);
        MSG_WriteLong(msg, time);
    }
    else
    {
        MSG_WriteBit0(msg);
        MSG_WriteBits(msg, timeBase - time, 8u);
    }
}

void __cdecl MSG_WriteDeltaFrameTime(int clientNum, msg_t *msg, int timeBase, int time)
{
    int delta; // [esp+0h] [ebp-4h]

    if ( msg->readOnly
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 950, 0, "%s", "!msg->readOnly") )
    {
        __debugbreak();
    }
    if ( time % 50
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    951,
                    0,
                    "%s",
                    "!(time % FRAME_MSEC)") )
    {
        __debugbreak();
    }
    if ( timeBase % 50
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    952,
                    0,
                    "%s",
                    "!(timeBase % FRAME_MSEC)") )
    {
        __debugbreak();
    }
    delta = (time - timeBase) / 50;
    if ( delta )
    {
        if ( delta > 0 || delta <= -8 )
        {
            if ( delta > 0 || delta <= -256 )
            {
                MSG_WriteBits(msg, 7, 3u);
                MSG_WriteLong(msg, time);
            }
            else
            {
                MSG_WriteBits(msg, 3, 3u);
                MSG_WriteBits(msg, (time - timeBase) / -50, 8u);
            }
        }
        else
        {
            MSG_WriteBits(msg, 1, 2u);
            MSG_WriteBits(msg, (time - timeBase) / -50, 3u);
        }
    }
    else
    {
        MSG_WriteBit0(msg);
    }
}

void __cdecl MSG_WriteEFlags(int clientNum, msg_t *msg, int oldFlags, int newFlags)
{
    unsigned int changedBitIndex; // [esp+0h] [ebp-Ch]
    int flagDiff; // [esp+4h] [ebp-8h]

    if ( msg->readOnly
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 993, 0, "%s", "!msg->readOnly") )
    {
        __debugbreak();
    }
    flagDiff = (newFlags ^ oldFlags) & 0xFFFFFF;
    if ( flagDiff && (flagDiff & (flagDiff - 1)) == 0 )
    {
        changedBitIndex = 0;
        while ( (flagDiff & 1) == 0 )
        {
            ++changedBitIndex;
            flagDiff >>= 1;
        }
        if ( changedBitIndex >= 0x18
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        1019,
                        0,
                        "%s\n\t(changedBitIndex) = %i",
                        "(changedBitIndex >= 0 && changedBitIndex < 24)",
                        changedBitIndex) )
        {
            __debugbreak();
        }
        if (1 << changedBitIndex != ((newFlags ^ oldFlags) & 0xFFFFFF)
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        1020,
                        0,
                        "%s",
                        "( ( ( oldFlags ^ newFlags ) & MASK_EFLAGS ) ^ ( 1 << changedBitIndex ) ) == 0") )
        {
            __debugbreak();
        }
        MSG_WriteBit0(msg);
        MSG_WriteBits(msg, changedBitIndex, 5u);
        if ( msg->overflowed )
        {
            if ( !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                            1029,
                            0,
                            "%s",
                            "!msg->overflowed") )
                __debugbreak();
        }
    }
    else
    {
        MSG_WriteBit1(msg);
        MSG_WriteBits(msg, newFlags, 0x18u);
    }
}

void __cdecl MSG_WriteDeltaAngle(const SnapshotInfo_s *snapInfo, msg_t *msg, float oldFloat, float fullFloat)
{
    unsigned int MinBitCountForNum; // eax
    int sign; // [esp+10h] [ebp-1Ch]
    int diff; // [esp+14h] [ebp-18h]
    int absdiff; // [esp+1Ch] [ebp-10h]
    int index; // [esp+24h] [ebp-8h]
    bool sent; // [esp+2Bh] [ebp-1h]

    diff = (__int16)(int)(float)(fullFloat * 182.04445) - (__int16)(int)(float)(oldFloat * 182.04445);
    absdiff = abs(diff);
    sent = 0;
    for ( index = 0; index < 7; ++index )
    {
        if ( g_commonAngleDeltas[index] == absdiff )
        {
            sent = 1;
            MSG_WriteBit1(msg);
            if ( diff >= 0 )
                MSG_WriteBit0(msg);
            else
                MSG_WriteBit1(msg);
            MinBitCountForNum = GetMinBitCountForNum(7u);
            MSG_WriteBits(msg, index, MinBitCountForNum);
            break;
        }
    }
    if ( !sent )
    {
        MSG_WriteBit0(msg);
        if ( absdiff && absdiff < 4096 )
        {
            sign = 1;
            MSG_WriteBit1(msg);
            if ( diff < 0 )
                sign = -1;
            if ( sign == -1 )
                MSG_WriteBit1(msg);
            else
                MSG_WriteBit0(msg);
            MSG_WriteBits(msg, absdiff, 0xCu);
        }
        else
        {
            MSG_WriteBit0(msg);
            MSG_WriteAngle16(msg, fullFloat);
        }
    }
}

char __cdecl MSG_WriteDeltaField(
                const SnapshotInfo_s *snapInfo,
                msg_t *msg,
                int time,
                const unsigned __int8 *from,
                const unsigned __int8 *to,
                const NetField *field,
                int fieldNum,
                bool forceSend,
                bool xorValue,
                int lastChangedField,
                int skippedFieldBits,
                bool sendSkippedFields)
{
    const char *EntityTypeString; // eax
    const char *v14; // [esp+0h] [ebp-14h]
    int zeroVal; // [esp+4h] [ebp-10h] BYREF
    const int *realFromF; // [esp+8h] [ebp-Ch]
    const int *toF; // [esp+Ch] [ebp-8h]
    const int *fromF; // [esp+10h] [ebp-4h]

    if ( msg->readOnly
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2205,
                    0,
                    "%s",
                    "!msg->readOnly") )
    {
        __debugbreak();
    }
    toF = (const int *)&to[field->offset];
    if ( !from && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 2213, 0, "%s", "from") )
        __debugbreak();
    realFromF = (const int *)&from[field->offset];
    if ( xorValue )
    {
        fromF = realFromF;
    }
    else
    {
        zeroVal = 0;
        fromF = &zeroVal;
    }
    if ( sendSkippedFields )
    {
        if ( !forceSend && MSG_ValuesAreEqual(snapInfo->clientNum, field->bits, field->size, realFromF, toF) )
            return 0;
        if ( !forceSend
            && *realFromF == *toF
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        2232,
                        0,
                        "%s",
                        "forceSend || *realFromF != *toF") )
        {
            __debugbreak();
        }
        if ( field->changeHints == 1 && (!snapInfo->fromBaseline || fieldNum) )
        {
            if ( snapInfo->packetEntityTypeArchived )
                v14 = "archived";
            else
                v14 = "unarchived";
            EntityTypeString = SV_GetEntityTypeString(snapInfo->packetEntityType);
            Com_PrintError(
                15,
                "Field %s changed for %s eType %s when we thought it never would\n",
                field->name,
                v14,
                EntityTypeString);
        }
        MSG_EncodeSkippedFields(snapInfo, msg, fieldNum - lastChangedField, skippedFieldBits);
    }
    return MSG_WriteDeltaField_Internal(snapInfo, msg, time, from, to, field, fieldNum, fromF, toF);
}

void __cdecl MSG_EncodeSkippedFields(
                const SnapshotInfo_s *snapInfo,
                msg_t *msg,
                int numFieldsSkipped,
                int skippedFieldBits)
{
    int numFieldsSkippeda; // [esp+10h] [ebp+10h]

    if ( numFieldsSkipped <= 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    1494,
                    0,
                    "%s\n\t(numFieldsSkipped) = %i",
                    "(numFieldsSkipped > 0)",
                    numFieldsSkipped) )
    {
        __debugbreak();
    }
    if ( skippedFieldBits == 1 )
    {
        while ( numFieldsSkipped > 1 )
        {
            MSG_WriteBit0(msg);
            --numFieldsSkipped;
        }
        goto LABEL_9;
    }
    if ( numFieldsSkipped == 1 )
    {
LABEL_9:
        MSG_WriteBit1(msg);
        return;
    }
    MSG_WriteBit0(msg);
    for ( numFieldsSkippeda = numFieldsSkipped - 1;
                numFieldsSkippeda >= (1 << skippedFieldBits) - 1;
                numFieldsSkippeda -= (1 << skippedFieldBits) - 1 )
    {
        MSG_WriteBits(msg, -1, skippedFieldBits);
    }
    MSG_WriteBits(msg, numFieldsSkippeda, skippedFieldBits);
}

void __cdecl MSG_WriteDeltaFields(
                const SnapshotInfo_s *snapInfo,
                msg_t *msg,
                int time,
                const unsigned __int8 *from,
                const unsigned __int8 *to,
                int force,
                int lastChanged,
                int numFields,
                const NetField *stateFields,
                int skippedFieldBits,
                bool writeEntUnlinkBit)
{
    const char *v11; // eax
    bool v12; // [esp+0h] [ebp-30h]
    int v13; // [esp+4h] [ebp-2Ch]
    bool v14; // [esp+Ch] [ebp-24h]
    int bits; // [esp+10h] [ebp-20h]
    const NetField *field; // [esp+1Ch] [ebp-14h]
    const NetField *fielda; // [esp+1Ch] [ebp-14h]
    int i; // [esp+24h] [ebp-Ch]
    int ia; // [esp+24h] [ebp-Ch]
    int lastChangeda; // [esp+50h] [ebp+20h]
    int lastChangedb; // [esp+50h] [ebp+20h]

    if ( msg->readOnly
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2270,
                    0,
                    "%s",
                    "!msg->readOnly") )
    {
        __debugbreak();
    }
    if ( force || lastChanged >= 0 )
    {
        MSG_WriteBit1(msg);
        if ( lastChanged < 0
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        2296,
                        0,
                        "%s",
                        "lastChanged >= 0") )
        {
            __debugbreak();
        }
        if ( writeEntUnlinkBit )
        {
            if ( snapInfo->entJustUnlinked )
                MSG_WriteBit1(msg);
            else
                MSG_WriteBit0(msg);
        }
        MSG_WriteLastChangedField(msg, lastChanged, numFields);
        lastChangeda = -1;
        for ( i = 0; i <= lastChanged; ++i )
        {
            field = &stateFields[i];
            if ( field->changeHints != 2 || i == lastChanged )
            {
                v14 = 0;
                if ( snapInfo->entJustUnlinked )
                {
                    bits = field->bits;
                    if ( bits >= -66 && bits <= -61 )
                        v14 = 1;
                }
                if ( MSG_WriteDeltaField(snapInfo, msg, time, from, to, field, i, v14, !v14, lastChangeda, skippedFieldBits, 1) )
                    lastChangeda = i;
            }
        }
        if ( lastChangeda != lastChanged
            && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                        2334,
                        0,
                        "%s",
                        "lastChanged == numFields") )
        {
            __debugbreak();
        }
        lastChangedb = -1;
        for ( ia = 0; ia < lastChanged; ++ia )
        {
            fielda = &stateFields[ia];
            if ( fielda->changeHints == 2 )
            {
                v12 = 0;
                if ( snapInfo->entJustUnlinked )
                {
                    v13 = fielda->bits;
                    if ( v13 >= -66 && v13 <= -61 )
                        v12 = 1;
                }
                if ( MSG_WriteDeltaField(snapInfo, msg, time, from, to, fielda, ia, 1, !v12, lastChangedb, skippedFieldBits, 0) )
                {
                    lastChangedb = ia;
                }
                else
                {
                    v11 = va("An ALWAYS_CHANGES field (%s) didn't have its delta written?", fielda->name);
                    if ( !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 2355, 0, v11) )
                        __debugbreak();
                }
            }
        }
    }
    else
    {
        MSG_WriteBit0(msg);
    }
}

void __cdecl MSG_WriteEntityRemoval(
                SnapshotInfo_s *snapInfo,
                msg_t *msg,
                unsigned __int8 *from,
                int indexBits,
                bool changeBit)
{
    if ( !from && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 2367, 0, "%s", "from") )
        __debugbreak();
    if ( msg->readOnly
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2368,
                    0,
                    "%s",
                    "!msg->readOnly") )
    {
        __debugbreak();
    }
    if ( cl_shownet && (cl_shownet->current.integer >= 2 || cl_shownet->current.integer == -1) )
        Com_Printf(15, "W|%3i: #%-3i remove\n", msg->cursize, *(unsigned int *)from);
    if ( changeBit )
        MSG_WriteBit1(msg);
    MSG_WriteEntityIndex(snapInfo, msg, *(unsigned int *)from, indexBits);
    MSG_WriteBit1(msg);
}

int __cdecl MSG_WriteEntityDeltaForEType(
                SnapshotInfo_s *snapInfo,
                msg_t *msg,
                int time,
                int eType,
                const entityState_s *from,
                const entityState_s *to,
                DeltaFlags flags)
{
    int bits; // [esp+0h] [ebp-8h]
    const NetFieldList *fieldList; // [esp+4h] [ebp-4h]

    if ( msg->readOnly
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2407,
                    0,
                    "%s",
                    "!msg->readOnly") )
    {
        __debugbreak();
    }
    fieldList = MSG_GetStateFieldListForEntityType(eType, snapInfo->demoSnapshot);
    s_bo1WriteEnt = to; // mod (L51): named by the far-origin Com_Errors
    s_bo1WriteSnap = snapInfo;
    bits = MSG_WriteEntityDelta(snapInfo, msg, time, from, to, flags, fieldList->count, g_entNumBits, fieldList->array); // mod (L25): networked entity numbers 1537+
    s_bo1WriteEnt = NULL;
    s_bo1WriteSnap = NULL;
    SV_PacketAnalyze_TrackETypeBytes(eType, to->number, bits, snapInfo->archived, fieldList);
    return bits;
}

bool __cdecl MSG_TrTypeUsesTrDuration(int trType)
{
    bool result; // al

    switch ( trType )
    {
        case 0:
        case 1:
        case 3:
        case 6:
        case 10:
        case 12:
        case 13:
        case 14:
            result = 0;
            break;
        default:
            result = 1;
            break;
    }
    return result;
}

bool __cdecl MSG_ShouldEntityFieldBeForcedHidden(
                const SnapshotInfo_s *snapInfo,
                const entityState_s *from,
                const entityState_s *to,
                const NetField *field)
{
    bool result; // al
    bool v5; // [esp+0h] [ebp-14h]
    bool v6; // [esp+4h] [ebp-10h]
    bool v7; // [esp+8h] [ebp-Ch]
    bool v8; // [esp+Ch] [ebp-8h]

    switch ( field->bits )
    {
        case 0xFFFFFFBB:
            v5 = !MSG_EntityIsLinked(to) && !MSG_TrTypeUsesTrDuration(to->lerp.apos.trType);
            result = v5;
            break;
        case 0xFFFFFFBD:
            v6 = !MSG_EntityIsLinked(to) && !MSG_TrTypeUsesTrDuration(to->lerp.pos.trType);
            result = v6;
            break;
        case 0xFFFFFFBE:
        case 0xFFFFFFBF:
        case 0xFFFFFFC0:
        case 0xFFFFFFC2:
            result = MSG_EntityIsLinked(to);
            break;
        case 0xFFFFFFC1:
            v8 = !MSG_EntityIsLinked(to) && !Flame_GetLocalClientSourceRange();
            result = v8;
            break;
        case 0xFFFFFFC3:
            v7 = !MSG_EntityIsLinked(to) && !Flame_GetLocalClientSourceRange();
            result = v7;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

bool __cdecl MSG_ShouldSendEntityField(
                const SnapshotInfo_s *snapInfo,
                const entityState_s *from,
                const entityState_s *to,
                const NetField *field)
{
    if ( MSG_ShouldEntityFieldBeForcedSent(snapInfo, from, to, field) )
        return 1;
    if ( MSG_ShouldEntityFieldBeForcedHidden(snapInfo, from, to, field) )
        return 0;
    return !MSG_ValuesAreEqual(
                        snapInfo->clientNum,
                        field->bits,
                        field->size,
                        (const int *)((char *)&from->number + field->offset),
                        (const int *)((char *)&to->number + field->offset));
}

bool __cdecl MSG_ShouldEntityFieldBeForcedSent(
                const SnapshotInfo_s *snapInfo,
                const entityState_s *from,
                const entityState_s *to,
                const NetField *field)
{
    bool result; // al
    bool v5; // [esp+0h] [ebp-14h]
    bool v6; // [esp+4h] [ebp-10h]
    bool v7; // [esp+8h] [ebp-Ch]
    bool v8; // [esp+Ch] [ebp-8h]

    switch ( field->bits )
    {
        case 0xFFFFFFBB:
            v5 = !snapInfo->fromBaseline
                && !MSG_TrTypeUsesTrDuration(from->lerp.apos.trType)
                && MSG_TrTypeUsesTrDuration(to->lerp.apos.trType);
            result = v5;
            break;
        case 0xFFFFFFBD:
            v6 = !snapInfo->fromBaseline
                && !MSG_TrTypeUsesTrDuration(from->lerp.pos.trType)
                && MSG_TrTypeUsesTrDuration(to->lerp.pos.trType);
            result = v6;
            break;
        case 0xFFFFFFBE:
        case 0xFFFFFFBF:
        case 0xFFFFFFC0:
        case 0xFFFFFFC2:
            result = snapInfo->entJustUnlinked;
            break;
        case 0xFFFFFFC1:
            v8 = snapInfo->entJustUnlinked && Flame_GetLocalClientSourceRange();
            result = v8;
            break;
        case 0xFFFFFFC3:
            v7 = snapInfo->entJustUnlinked && Flame_GetLocalClientSourceRange();
            result = v7;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

int __cdecl MSG_WriteEntity(
                SnapshotInfo_s *snapInfo,
                msg_t *msg,
                int time,
                entityState_s *from,
                const entityState_s *to,
                DeltaFlags flags)
{
    PacketEntityType v6; // eax
    PacketEntityType PacketEntityTypeForEType; // eax
    bool v9; // [esp+0h] [ebp-Ch]
    int UsedBitCount; // [esp+4h] [ebp-8h]

    if ( msg->readOnly
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2537,
                    0,
                    "%s",
                    "!msg->readOnly") )
    {
        __debugbreak();
    }
    if ( to )
    {
        PacketEntityTypeForEType = MSG_GetPacketEntityTypeForEType(to->eType);
        MSG_PacketAnalyze_SetPacketEntityType(snapInfo, PacketEntityTypeForEType, 0);
        v9 = from && !MSG_EntityIsLinked(to) && MSG_EntityIsLinked(from);
        snapInfo->entJustUnlinked = v9;
        return MSG_WriteEntityDeltaForEType(snapInfo, msg, time, to->eType, from, to, flags);
    }
    else
    {
        if ( !from
            && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 2542, 0, "%s", "from") )
        {
            __debugbreak();
        }
        v6 = MSG_GetPacketEntityTypeForEType(from->eType);
        MSG_PacketAnalyze_SetPacketEntityType(snapInfo, v6, 0);
        UsedBitCount = MSG_GetUsedBitCount(msg);
        MSG_WriteEntityRemoval(snapInfo, msg, (unsigned __int8 *)from, g_entNumBits, 0); // mod (L25): networked entity numbers 1537+
        return MSG_GetUsedBitCount(msg) - UsedBitCount;
    }
}

void __cdecl MSG_WriteDeltaClient(
                SnapshotInfo_s *snapInfo,
                msg_t *msg,
                int time,
                clientState_s *from,
                clientState_s *to,
                int force)
{
    unsigned int numFields; // [esp+0h] [ebp-E4h]
    clientState_s dummy; // [esp+4h] [ebp-E0h] BYREF
    int bits; // [esp+DCh] [ebp-8h]
    const NetField *fields; // [esp+E0h] [ebp-4h]

    if ( msg->readOnly
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2586,
                    0,
                    "%s",
                    "!msg->readOnly") )
    {
        __debugbreak();
    }
    fields = MSG_GetNetFieldList(NET_FIELD_TYPE_CLIENT_STATE, snapInfo->demoSnapshot)->array;
    numFields = MSG_GetNetFieldList(NET_FIELD_TYPE_CLIENT_STATE, snapInfo->demoSnapshot)->count;
    if ( !from )
    {
        from = &dummy;
        memset((unsigned __int8 *)&dummy, 0, sizeof(dummy));
    }
    MSG_PacketAnalyze_SetPacketEntityType(snapInfo, ANALYZE_DATATYPE_ENTITYTYPE_CLIENTSTATE, 0);
    if ( to )
    {
        bits = MSG_WriteDeltaStruct(
                         snapInfo,
                         msg,
                         time,
                         (unsigned __int8 *)from,
                         (unsigned __int8 *)to,
                         force,
                         numFields,
                         5,
                         fields,
                         1,
                         1);
        if ( bits && sv_debugPacketContentsQuick->current.integer > 0 && !snapInfo->archived )
        {
            CG_QuickPrint("Client %i delta %i bits\n", to->clientIndex, bits);
            sv_quickBitsTotal += bits;
        }
    }
    else
    {
        MSG_WriteEntityRemoval(snapInfo, msg, (unsigned __int8 *)from, 5, 1);
    }
}

bool do_it = true;
bool __cdecl MSG_WithinAllowedPredictionError(float dist, const playerState_s *to)
{
    if ( (to->eFlags & 0x4000) != 0 && do_it )
        return dist <= 100.0;
    else
        return dist <= 0.0099999998;
}

int __cdecl MSG_GetLastChangedField(
                const SnapshotInfo_s *snapInfo,
                const unsigned __int8 *from,
                const unsigned __int8 *to,
                int numFields,
                const NetField *stateFields)
{
    const NetField *field; // [esp+0h] [ebp-14h]
    int lc; // [esp+4h] [ebp-10h]
    int i; // [esp+10h] [ebp-4h]

    lc = 0;
    i = numFields - 1;
    field = &stateFields[numFields - 1];
    while ( i >= 0 )
    {
        if ( !MSG_ValuesAreEqual(
                        snapInfo->clientNum,
                        field->bits,
                        field->size,
                        (const int *)&from[field->offset],
                        (const int *)&to[field->offset]) )
        {
            lc = i + 1;
            break;
        }
        --i;
        --field;
    }
    if ( (lc < 0 || lc > numFields)
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2672,
                    0,
                    "%s\n\t(lc) = %i",
                    "(lc >= 0 && lc <= numFields)",
                    lc) )
    {
        __debugbreak();
    }
    return lc;
}

playerState_s dummy_1;
void __cdecl MSG_WriteDeltaPlayerstate(
                SnapshotInfo_s *snapInfo,
                msg_t *msg,
                int time,
                const playerState_s *from,
                const playerState_s *to)
{
    int UsedBitCount; // eax
    const char *v6; // eax
    int v7; // eax
    int v8; // eax
    int v9; // eax
    int v10; // eax
    int v11; // eax
    int v12; // eax
    int v13; // eax
    bool v14; // [esp+10h] [ebp-A4h]
    bool v15; // [esp+14h] [ebp-A0h]
    int j; // [esp+30h] [ebp-84h]
    int ja; // [esp+30h] [ebp-84h]
    int jb; // [esp+30h] [ebp-84h]
    int jc; // [esp+30h] [ebp-84h]
    int ammoCount; // [esp+34h] [ebp-80h]
    int lastChanged; // [esp+38h] [ebp-7Ch]
    int lastChangeda; // [esp+38h] [ebp-7Ch]
    unsigned int numObjFields; // [esp+3Ch] [ebp-78h]
    signed int numFields; // [esp+40h] [ebp-74h]
    const NetField *field; // [esp+44h] [ebp-70h]
    const NetField *fielda; // [esp+44h] [ebp-70h]
    const NetField *fieldb; // [esp+44h] [ebp-70h]
    float dist; // [esp+48h] [ebp-6Ch]
    int clipCount; // [esp+4Ch] [ebp-68h]
    int lc; // [esp+50h] [ebp-64h]
    int lca; // [esp+50h] [ebp-64h]
    const NetField *objFields; // [esp+54h] [ebp-60h]
    bool needToSendWeapon[16]; // [esp+58h] [ebp-5Ch] BYREF
    int statsbits; // [esp+6Ch] [ebp-48h]
    bool forceSend; // [esp+71h] [ebp-43h]
    bool anyWeaponChanged; // [esp+73h] [ebp-41h]
    const NetField *psFields; // [esp+74h] [ebp-40h]
    int fieldsChanged[12]; // [esp+78h] [ebp-3Ch] BYREF
    int i; // [esp+A8h] [ebp-Ch]
    int lastAlwaysChangesField; // [esp+ACh] [ebp-8h]
    bool sendOriginAndVel; // [esp+B3h] [ebp-1h]

    i = 0;
    lc = -1;
    if ( msg->readOnly
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2705,
                    0,
                    "%s",
                    "!msg->readOnly") )
    {
        __debugbreak();
    }
    memset((unsigned __int8 *)fieldsChanged, 0, sizeof(fieldsChanged));
    UsedBitCount = MSG_GetUsedBitCount(msg);
    SV_PacketAnalyze_TrackPS_ClearBits(UsedBitCount);
    MSG_PacketAnalyze_SetPacketEntityType(snapInfo, ANALYZE_DATATYPE_ENTITYTYPE_PLAYERSTATE, 0);
    if ( !from )
    {
        from = &dummy_1;
        memset((unsigned __int8 *)&dummy_1, 0, sizeof(dummy_1));
    }
    if ( snapInfo->demoSnapshot || snapInfo->archived )
        goto LABEL_17;
    dist = Vec3DistanceSq(snapInfo->client->predictedOrigin, to->origin);
    if ( !svsHeaderValid
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2736,
                    0,
                    "%s",
                    "svsHeaderValid") )
    {
        __debugbreak();
    }
    if ( from
        && svsHeader.clientArchive
        && MSG_WithinAllowedPredictionError(dist, to)
        && snapInfo->client->predictedOriginServerTime == to->commandTime )
    {
        sendOriginAndVel = 0;
        MSG_WriteBit0(msg);
    }
    else
    {
LABEL_17:
        sendOriginAndVel = 1;
        MSG_WriteBit1(msg);
    }
    psFields = MSG_GetNetFieldList(NET_FIELD_TYPE_PLAYERSTATE, snapInfo->demoSnapshot)->array;
    numFields = MSG_GetNetFieldList(NET_FIELD_TYPE_PLAYERSTATE, snapInfo->demoSnapshot)->count;
    i = 0;
    field = psFields;
    while ( i < numFields )
    {
        if ( MSG_ShouldSendPSField(snapInfo, sendOriginAndVel, to, from, field) )
        {
            fieldsChanged[i >> 5] |= 1 << (i & 0x1F);
            lc = i;
        }
        ++i;
        ++field;
    }
    if ( lc < -1
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp", 2780, 0, "%s", "lc >= -1") )
    {
        __debugbreak();
    }
    MSG_WriteLastChangedField(msg, lc + 1, numFields + 1);
    lastChanged = -1;
    lastAlwaysChangesField = -1;
    i = 0;
    fielda = psFields;
    while ( i <= lc )
    {
        if ( fielda->changeHints != 2 || i == lc )
        {
            if ( (fieldsChanged[i >> 5] & (1 << (i & 0x1F))) != 0 )
            {
                v15 = !snapInfo->archived && !snapInfo->demoSnapshot && fielda->changeHints == 3;
                forceSend = v15;
                if ( !MSG_WriteDeltaField(
                                snapInfo,
                                msg,
                                time,
                                (const unsigned __int8 *)from,
                                (const unsigned __int8 *)to,
                                fielda,
                                i,
                                1,
                                !v15,
                                lastChanged,
                                4,
                                1) )
                {
                    v6 = va("field %s changed but wasn't sent to the client\n", fielda->name);
                    if ( !Assert_MyHandler(
                                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                                    2797,
                                    0,
                                    "%s\n\t%s",
                                    "sent",
                                    v6) )
                        __debugbreak();
                }
                lastChanged = i;
            }
        }
        else
        {
            lastAlwaysChangesField = i;
        }
        ++i;
        ++fielda;
    }
    if ( lc >= 0
        && lastChanged > lc
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2803,
                    0,
                    "%s",
                    "lastChanged <= lc") )
    {
        __debugbreak();
    }
    lastChangeda = -1;
    i = 0;
    fieldb = psFields;
    while ( i <= lastAlwaysChangesField )
    {
        if ( fieldb->changeHints == 2 )
        {
            v14 = !snapInfo->archived && !snapInfo->demoSnapshot && fieldb->changeHints == 3;
            forceSend = v14;
            if ( MSG_WriteDeltaField(
                         snapInfo,
                         msg,
                         time,
                         (const unsigned __int8 *)from,
                         (const unsigned __int8 *)to,
                         fieldb,
                         i,
                         1,
                         !v14,
                         lastChangeda,
                         4,
                         0) )
            {
                lastChangeda = i;
            }
        }
        ++i;
        ++fieldb;
    }
    if ( lastChangeda > lc
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    2823,
                    0,
                    "%s",
                    "lastChanged <= lc") )
    {
        __debugbreak();
    }
    v7 = MSG_GetUsedBitCount(msg);
    SV_PacketAnalyze_TrackPS_FieldDeltasBits(v7);
    statsbits = 0;
    for ( i = 0; i < 5; ++i )
    {
        if ( to->stats[i] != from->stats[i] )
            statsbits |= 1 << i;
    }
    if ( statsbits )
    {
        MSG_WriteBit1(msg);
        MSG_WriteBits(msg, statsbits, 5u);
        if ( (statsbits & 1) != 0 )
            MSG_WriteShort(msg, to->stats[0]);
        if ( (statsbits & 2) != 0 )
            MSG_WriteShort(msg, to->stats[1]);
        if ( (statsbits & 4) != 0 )
            MSG_WriteShort(msg, to->stats[2]);
        if ( (statsbits & 8) != 0 )
            MSG_WriteBits(msg, to->stats[3], 5u);
        if ( (statsbits & 0x10) != 0 )
            MSG_WriteByte(msg, to->stats[4]);
    }
    else
    {
        MSG_WriteBit0(msg);
    }
    v8 = MSG_GetUsedBitCount(msg);
    SV_PacketAnalyze_TrackPS_StatsBits(v8);
    memset(needToSendWeapon, 0, 15);
    anyWeaponChanged = 0;
    for ( j = 0; j < 15; ++j )
    {
        if ( from->heldWeapons[j].weapon == to->heldWeapons[j].weapon )
        {
            if ( memcmp(&to->heldWeapons[j], &from->heldWeapons[j], 0x18u) )
            {
                anyWeaponChanged = 1;
                needToSendWeapon[j] = 1;
            }
        }
        else
        {
            anyWeaponChanged = 1;
            needToSendWeapon[j] = 1;
        }
    }
    if ( anyWeaponChanged )
    {
        MSG_WriteBit1(msg);
        for ( ja = 0; ja < 15; ++ja )
        {
            if ( !needToSendWeapon[ja] )
                goto LABEL_92;
            MSG_WriteBit1(msg);
            if ( from->heldWeapons[ja].weapon == to->heldWeapons[ja].weapon )
            {
                MSG_WriteBit0(msg);
            }
            else
            {
                MSG_WriteBit1(msg);
                MSG_WriteBits(msg, to->heldWeapons[ja].weapon, 0xBu);
            }
            if ( from->heldWeapons[ja].model == to->heldWeapons[ja].model )
            {
                MSG_WriteBit0(msg);
            }
            else
            {
                MSG_WriteBit1(msg);
                MSG_WriteBits(msg, to->heldWeapons[ja].model, 4u);
            }
            if ( from->heldWeapons[ja].options.i == to->heldWeapons[ja].options.i )
            {
                MSG_WriteBit0(msg);
            }
            else
            {
                MSG_WriteBit1(msg);
                MSG_WriteBits(msg, to->heldWeapons[ja].options.i, 0x20u);
            }
            if ( from->heldWeapons[ja].fuelTankTime == to->heldWeapons[ja].fuelTankTime )
            {
                MSG_WriteBit0(msg);
            }
            else
            {
                MSG_WriteBit1(msg);
                MSG_WriteBits(msg, to->heldWeapons[ja].fuelTankTime, 0x20u);
            }
            if ( to->heldWeapons[ja].overHeating )
                MSG_WriteBit1(msg);
            else
                MSG_WriteBit0(msg);
            if ( to->heldWeapons[ja].needsRechamber )
                MSG_WriteBit1(msg);
            else
                MSG_WriteBit0(msg);
            if ( to->heldWeapons[ja].heldBefore )
                MSG_WriteBit1(msg);
            else
                MSG_WriteBit0(msg);
            if ( to->heldWeapons[ja].quickReload )
                MSG_WriteBit1(msg);
            else
LABEL_92:
                MSG_WriteBit0(msg);
        }
    }
    else
    {
        MSG_WriteBit0(msg);
    }
    v9 = MSG_GetUsedBitCount(msg);
    SV_PacketAnalyze_TrackPS_WeaponBits(v9);
    for ( jb = 0; jb < 15; ++jb )
    {
        if ( memcmp(&to->ammoNotInClip[jb], &from->ammoNotInClip[jb], 8u) )
        {
            MSG_WriteBit1(msg);
            MSG_WriteBits(msg, jb, 4u);
            MSG_WriteBits(msg, to->ammoNotInClip[jb].ammoIndex, 0xBu);
            ammoCount = to->ammoNotInClip[jb].count;
            if ( ammoCount >= 1024 )
            {
                ammoCount = 1023;
                if ( !snapInfo->archived && sv_debugPacketContentsQuick->current.integer > 0 )
                    Com_Printf(15, "Pool ammo clamped to %i when packed into message.\n", 1024);
            }
            MSG_WriteBits(msg, ammoCount, 0xAu);
        }
    }
    MSG_WriteBit0(msg);
    v10 = MSG_GetUsedBitCount(msg);
    SV_PacketAnalyze_TrackPS_AmmoPoolBits(v10);
    for ( jc = 0; jc < 15; ++jc )
    {
        if ( memcmp(&to->ammoInClip[jc], &from->ammoInClip[jc], 8u) )
        {
            MSG_WriteBit1(msg);
            MSG_WriteBits(msg, jc, 4u);
            MSG_WriteBits(msg, to->ammoInClip[jc].clipIndex, 0xBu);
            clipCount = to->ammoInClip[jc].count;
            if ( clipCount >= 1024 )
            {
                clipCount = 1023;
                if ( !snapInfo->archived && sv_debugPacketContentsQuick->current.integer > 0 )
                    Com_Printf(15, "Clip ammo clamped to %i when packed into message.\n", 1024);
            }
            MSG_WriteBits(msg, clipCount, 0xAu);
        }
    }
    MSG_WriteBit0(msg);
    v11 = MSG_GetUsedBitCount(msg);
    SV_PacketAnalyze_TrackPS_AmmoClipBits(v11);
    if ( !memcmp(from->objective, to->objective, 0x600u) )
    {
        MSG_WriteBit0(msg);
    }
    else
    {
        MSG_WriteBit1(msg);
        for ( i = 0; i < 32; ++i )
        {
            MSG_WriteBits(msg, to->objective[i].state, 3u);
            objFields = MSG_GetNetFieldList(NET_FIELD_TYPE_OBJECTIVE, snapInfo->demoSnapshot)->array;
            numObjFields = MSG_GetNetFieldList(NET_FIELD_TYPE_OBJECTIVE, snapInfo->demoSnapshot)->count;
            lca = MSG_GetLastChangedField(
                            snapInfo,
                            (const unsigned __int8 *)&from->objective[i],
                            (const unsigned __int8 *)&to->objective[i],
                            numObjFields,
                            objFields);
            MSG_WriteDeltaFields(
                snapInfo,
                msg,
                time,
                (const unsigned __int8 *)&from->objective[i],
                (const unsigned __int8 *)&to->objective[i],
                0,
                lca - 1,
                numObjFields,
                objFields,
                1,
                0);
        }
    }
    v12 = MSG_GetUsedBitCount(msg);
    SV_PacketAnalyze_TrackPS_ObjectivesBits(v12);
    if ( !memcmp(&from->hud, &to->hud, 0x1B20u) )
    {
        MSG_WriteBit0(msg);
    }
    else
    {
        MSG_WriteBit1(msg);
        MSG_WriteDeltaHudElems(snapInfo, msg, time, from->hud.archival, to->hud.archival, 31);
        MSG_WriteDeltaHudElems(snapInfo, msg, time, from->hud.current, to->hud.current, 31);
    }
    v13 = MSG_GetUsedBitCount(msg);
    SV_PacketAnalyze_TrackPS_HudelemBits(v13);
}

bool __cdecl MSG_ShouldSendPSField(
                const SnapshotInfo_s *snapInfo,
                bool sendOriginAndVel,
                const playerState_s *ps,
                const playerState_s *oldPs,
                const NetField *field)
{
    int bits; // [esp+0h] [ebp-3Ch]

    if ( !snapInfo->demoSnapshot )
    {
        bits = field->bits;
        if ( bits == -87 )
        {
            if ( !snapInfo->archived )
            {
                if ( (ps->otherFlags & 2) != 0 && (oldPs->otherFlags & 2) == 0 && oldPs->clientNum == ps->clientNum )
                    return 1;
                if ( (ps->otherFlags & 2) == 0
                    && ((oldPs->eFlags ^ ps->eFlags) & 2) == 0
                    && ps->viewlocked_entNum == 1023
                    && ps->pm_type != 5 )
                {
                    return 0;
                }
            }
        }
        else
        {
            if ( bits == -71 && ps->fWeaponPosFrac != 0.0 )
                return 1;
            if ( field->changeHints == 3 && !snapInfo->archived )
                return sendOriginAndVel;
        }
    }
    return !MSG_ValuesAreEqual(
                        snapInfo->clientNum,
                        field->bits,
                        field->size,
                        (const int *)((char *)&oldPs->commandTime + field->offset),
                        (const int *)((char *)&ps->commandTime + field->offset));
}

// local variable allocation has failed, the output may be wrong!
void    MSG_WriteDeltaMatchState(
                SnapshotInfo_s *snapInfo,
                msg_t *msg,
                int time,
                MatchState *from,
                MatchState *to)
{
    unsigned __int8 v6[128]; // [esp-Ch] [ebp-10Ch] OVERLAPPED BYREF
    unsigned int count; // [esp+ECh] [ebp-14h]
    const NetField *array; // [esp+F0h] [ebp-10h]
    //int v9; // [esp+F4h] [ebp-Ch]
    //int numFields; // [esp+F8h] [ebp-8h]
    //int retaddr; // [esp+100h] [ebp+0h]

    //v9 = a1;
    //numFields = retaddr;
    MSG_PacketAnalyze_SetPacketEntityType(snapInfo, ANALYZE_DATATYPE_ENTITYTYPE_MATCHSTATE, 0);
    array = MSG_GetNetFieldList(NET_FIELD_TYPE_MATCHSTATE, snapInfo->demoSnapshot)->array;
    count = MSG_GetNetFieldList(NET_FIELD_TYPE_MATCHSTATE, snapInfo->demoSnapshot)->count;
    if ( from )
    {
        MSG_WriteDeltaStruct(snapInfo, msg, time, (unsigned __int8 *)from, (unsigned __int8 *)to, 1, count, 1, array, 0, 1);
    }
    else
    {
        memset(v6, 0, sizeof(v6));
        MSG_WriteDeltaStruct(snapInfo, msg, time, v6, (unsigned __int8 *)to, 1, count, 1, array, 0, 1);
    }
}

int __cdecl MSG_WriteDeltaHudElems_LastChangedField(
                SnapshotInfo_s *snapInfo,
                int i,
                const hudelem_s *from,
                const hudelem_s *to)
{
    int j; // [esp+0h] [ebp-18h]
    signed int numFields; // [esp+4h] [ebp-14h]
    int lc; // [esp+8h] [ebp-10h]
    const NetField *fields; // [esp+14h] [ebp-4h]

    lc = -1;
    fields = MSG_GetNetFieldList(NET_FIELD_TYPE_HUDELEMS, snapInfo->demoSnapshot)->array;
    numFields = MSG_GetNetFieldList(NET_FIELD_TYPE_HUDELEMS, snapInfo->demoSnapshot)->count;
    for ( j = numFields - 1; j >= 0; --j )
    {
        if ( !MSG_ValuesAreEqual(
                        snapInfo->clientNum,
                        fields[j].bits,
                        fields[j].size,
                        (const int *)((char *)&from[i] + fields[j].offset),
                        (const int *)((char *)&to[i] + fields[j].offset)) )
        {
            lc = j;
            break;
        }
    }
    // L35: lc == numFields - 1 (the last field, "duration", changed) is a valid delta: the writer sends lc + 1 in 6 bits
    // and MSG_ReadDeltaHudElems reads fields 0..lc. MP scripts never change duration (HECmd_SetClock_Internal); Der
    // Riese's _zombiemode_timer SetClock does, and the strict "< numFields" asserted there (the retail exes carry no
    // asserts; SP's 45-row table 0x00A5D380 has two fields after duration). Keep the range check, allow the last field.
    if ( (lc + 1 < 0 || lc + 1 > numFields)
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    3232,
                    0,
                    "%s\n\t(lc) = %i",
                    "(lc+1 >= 0 && lc+1 < static_cast<int>( numFields ))",
                    lc) )
    {
        __debugbreak();
    }
    return lc;
}

void __cdecl MSG_WriteDeltaHudElems_ValidateHudElem(const hudelem_s *from, const hudelem_s *to)
{
    int alignY; // [esp+0h] [ebp-8h]
    int alignYa; // [esp+0h] [ebp-8h]
    unsigned int alignX; // [esp+4h] [ebp-4h]
    unsigned int alignXa; // [esp+4h] [ebp-4h]

    if ( (from->alignOrg & 0xF0) != 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    3244,
                    0,
                    "%s\n\t(from->alignOrg) = %i",
                    "(!(from->alignOrg & ~15))",
                    from->alignOrg) )
    {
        __debugbreak();
    }
    if ( (to->alignOrg & 0xF0) != 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    3245,
                    0,
                    "%s\n\t(from->alignOrg) = %i",
                    "(!(to->alignOrg & ~15))",
                    from->alignOrg) )
    {
        __debugbreak();
    }
    alignX = ((int)from->alignOrg >> 2) & 3;
    if ( alignX > 2
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    3248,
                    0,
                    "%s\n\t(from->alignOrg) = %i",
                    "(alignX == 0 || alignX == 1 || alignX == 2)",
                    from->alignOrg) )
    {
        __debugbreak();
    }
    alignY = from->alignOrg & 3;
    if ( (from->alignOrg & 3) != 0
        && alignY != 1
        && alignY != 2
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    3251,
                    0,
                    "%s\n\t(from->alignOrg) = %i",
                    "(alignY == 0 || alignY == 1 || alignY == 2)",
                    from->alignOrg) )
    {
        __debugbreak();
    }
    alignXa = ((int)to->alignOrg >> 2) & 3;
    if ( alignXa > 2
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    3254,
                    0,
                    "%s\n\t(to->alignOrg) = %i",
                    "(alignX == 0 || alignX == 1 || alignX == 2)",
                    to->alignOrg) )
    {
        __debugbreak();
    }
    alignYa = to->alignOrg & 3;
    if ( (to->alignOrg & 3) != 0
        && alignYa != 1
        && alignYa != 2
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\sv_msg_write.cpp",
                    3257,
                    0,
                    "%s\n\t(to->alignOrg) = %i",
                    "(alignY == 0 || alignY == 1 || alignY == 2)",
                    to->alignOrg) )
    {
        __debugbreak();
    }
}

