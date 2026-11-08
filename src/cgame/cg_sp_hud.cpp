// zombies: SP client HUD pieces BO1Zombies's MP client does not have. Everything here is reached only in
// zombiemode (the callers gate it). Each function names its SP address.
#include <game_sp/g_sp_loadgame.h>
#include "cg_sp_hud.h"
#include <cgame_mp/cg_local_mp.h>
#include <qcommon/common.h>
#include <qcommon/com_clients.h>
#include <universal/q_shared.h>
#include <universal/dvar.h>
#include <client/screen_placement.h>
#include <cgame_mp/cg_scoreboard_mp.h>
#include <ui/ui_main.h>
#include <ui/ui_atoms.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_material.h>
#include <ui/ui_shared.h>
#include <cgame/cg_drawtools.h>
#include <client_mp/cl_main_mp.h>
#include <bgame/bg_misc.h>
#include <game/teams.h>
#include <client_mp/cl_cgame_mp.h>
#include <win32/win_shared.h>
#include <sound/snd_public_async.h>
#include <game_sp/g_scr_sp_zombietron.h>
#include <stringed/stringed_hooks.h>
#include <cgame/cg_hudelem.h>
#include <gfx_d3d/r_rendercmds.h>
#include <server_mp/sv_init_mp.h>
#include <universal/com_math.h>
#include <cfloat>
#include <cmath>

// SP dvars BO1Zombies's MP client does not register (SP registers them at 0x005c7860..0x005c7ae6, in the
// same function as cg_scoreboardItemHeight / cg_scoreboardFont). Defaults and bounds are the exe's.
static const dvar_t *cg_ScoresColor_Player;
static const dvar_t *cg_ScoresColor_Transparency;
static const dvar_t *cg_ScoresColor_Zombie;
static const dvar_t *cg_ScoresColor_TransparencyZombie;

void CG_SP_RegisterScoreBarDvars()
{
    cg_ScoresColor_Player = _Dvar_RegisterColor("cg_ScoresColor_Player", 0.76f, 0.78f, 0.1f, 1.0f, 0, "");
    // the per-client score box colours CG_ScoreBarZombieColor reads by name (SP 0x005C78C9..0x005C7986)
    _Dvar_RegisterColor("cg_ScoresColor_Player_0", 0.49f, 0.49f, 0.1f, 1.0f, 0, "");
    _Dvar_RegisterColor("cg_ScoresColor_Player_1", 0.44f, 0.2f, 0.22f, 1.0f, 0, "");
    _Dvar_RegisterColor("cg_ScoresColor_Player_2", 0.16f, 0.72f, 0.28f, 1.0f, 0, "");
    _Dvar_RegisterColor("cg_ScoresColor_Player_3", 0.16f, 0.25f, 0.18f, 1.0f, 0, "");
    cg_ScoresColor_Transparency = _Dvar_RegisterFloat("cg_ScoresColor_Transparency", 0.8f, 0.0f, 1.0f, 0, "");
    cg_ScoresColor_Zombie = _Dvar_RegisterColor("cg_ScoresColor_Zombie", 0.21f, 0.0f, 0.0f, 1.0f, 0, "");
    cg_ScoresColor_TransparencyZombie = _Dvar_RegisterFloat("cg_ScoresColor_TransparencyZombie", 0.8f, 0.0f, 1.0f, 0, "");
    _Dvar_RegisterColor("cg_ScoresColor_Gamertag_0", 0.0f, 0.0f, 0.0f, 1.0f, 0, "");
    _Dvar_RegisterColor("cg_ScoresColor_Gamertag_1", 0.0f, 0.0f, 0.0f, 1.0f, 0, "");
    _Dvar_RegisterColor("cg_ScoresColor_Gamertag_2", 0.0f, 0.0f, 0.0f, 1.0f, 0, "");
    _Dvar_RegisterColor("cg_ScoresColor_Gamertag_3", 0.0f, 0.0f, 0.0f, 1.0f, 0, "");
}

// SP registers cg_scoreboardHeight 330 (SP 0x005c7724) and cg_scoreboardTextOffset 0.62 (SP 0x005c77c7), MP
// 435 and 0.6. BO1Zombies registers them at startup, before zombiemode is set by the zombie map, so a
// zombiemode default at registration never takes effect; CG_Init sets the SP values in zombiemode instead
// (as MP's CG_SetupSplitscreenDvars does for its HUD dvars). A later MP map in the same process keeps them.
void CG_SP_SetupScoreboardDvars()
{
    Dvar_SetFloat((dvar_s *)cg_scoreboardHeight, 330.0f);
    Dvar_SetFloat((dvar_s *)cg_scoreboardTextOffset, 0.62f);
}

// SP .rdata 0x00a60650: three constant column tables, 16 bytes a row (type, widthFrac, header, alignment).
struct scoreBarColumn_s
{
    int type;
    float widthFrac;
    const char *header;
    int alignment;
};
static const scoreBarColumn_s s_scoreBarColumnsDefault[2] = { { 12, 0.2f, "", 2 }, { 2, 0.8f, "", 2 } };   // 0x00a60650
static const scoreBarColumn_s s_scoreBarColumnsZombie[1] = { { 2, 1.0f, "", 0 } };                        // 0x00a60670

// SP 0x02ff830c: the score each client's bar shows, walked toward the real one a draw at a time.
static int s_scoreBarShown[32];
// SP 0x02ff8324: the rows, 8 bytes each ({clientNum, score}).
struct scoreBarRow_s
{
    int clientNum;
    int score;
};
static scoreBarRow_s s_scoreBarRows[32];

// CG_ScoreBarColumnValue (SP 0x00543270)
static int CG_ScoreBarColumnValue(int clientNum, int score)
{
    int delta = score - s_scoreBarShown[clientNum];
    if ( delta )
    {
        double step = (double)delta * 0.2 + 1.0;
        if ( (unsigned int)(delta - 1) < 0xF )
            step = 1.0;
        else if ( (unsigned int)(delta + 15) < 0xF )
            step = -1.0;
        s_scoreBarShown[clientNum] += (int)step;
        if ( (score < s_scoreBarShown[clientNum] && delta > 0) || (s_scoreBarShown[clientNum] < score && delta < 0) )
            s_scoreBarShown[clientNum] = score;
        score = s_scoreBarShown[clientNum];
    }
    return score;
}

// CG_ScoreBarZombieColor (SP 0x00890c80; name in EAX, clientNum in ECX, the colour out in ESI)
static void CG_ScoreBarZombieColor(const char *dvarName, int clientNum, float *color)
{
    char name[32];

    if ( !Dvar_GetBool("arcademode") && !zombiemode->current.enabled )
    {
        Dvar_GetUnpackedColor(cg_ScoresColor_Player, color);
        return;
    }
    switch ( clientNum )
    {
    case 0:
        Com_sprintf(name, 0x20, "%s_0", dvarName);
        break;
    case 1:
        Com_sprintf(name, 0x20, "%s_1", dvarName);
        break;
    case 2:
        Com_sprintf(name, 0x20, "%s_2", dvarName);
        break;
    case 3:
        Com_sprintf(name, 0x20, "%s_3", dvarName);
        break;
    default: // SP reads its uninitialised buffer here; clients 0-3 are the only ones SP zombies has
        name[0] = 0;
        break;
    }
    Dvar_GetUnpackedColorByName(name, color);
}

// CG_DrawScoreBarColumnText (SP 0x00892700; localClientNum in EAX, text in ESI, font in EDI)
static void CG_DrawScoreBarColumnText(
    int localClientNum,
    char *text,
    Font_s *font,
    float x,
    float y,
    float colWidth,
    int horzAlign,
    int vertAlign,
    int alignment,
    float scale,
    int style,
    const float *color,
    float rowHeight)
{
    const ScreenPlacement *scrPlace;
    float width;
    float dx;

    if ( !text )
        return;
    scrPlace = &scrPlaceView[localClientNum];
    while ( (float)UI_TextWidth(text, 0x7FFFFFFF, font, scale) > colWidth )
        scale = scale - 0.025f;
    if ( scale < 0.2f )
        style = 0;
    width = (float)UI_TextWidth(text, 0x7FFFFFFF, font, scale);
    if ( alignment == 1 )
        dx = (colWidth - width) * 0.5f;
    else if ( alignment == 2 )
        dx = colWidth - width - 4.0f;
    else
        dx = 0.0f;
    y = ((float)R_TextHeight(font) * scale + rowHeight) * cg_scoreboardTextOffset->current.value + y;
    UI_DrawText(scrPlace, text, 0x7FFFFFFF, font, x + dx, y, horzAlign, vertAlign, scale, color, style);
}

// SP .rdata 0x00a60680: the zombietron table. Types: 10 lives (score.revives), 8 bombs (score.assists), 9 boosters
// (score.downs), 2 points, 12 score multiplier and its bar, 11 the player's name with the weapon-time / revive
// meter (width 0: drawn from the bar's own x). _zombietron_score::update_hud copies lives / bombs / boosters into
// those fields "because they are sent across the network and not used in this game mode".
static const scoreBarColumn_s s_scoreBarColumnsZombietron[6] = {
    { 10, 0.1f, "", 0 }, { 8, 0.1f, "", 0 }, { 9, 0.3f, "", 0 }, { 2, 0.4f, "", 2 }, { 12, 0.1f, "", 0 },
    { 11, 0.0f, "", 0 } };

// SP cg+0xce514..0xce528: the zombietron score bar's state, one per local client (shared by every player's bar).
struct zombietronScoreBarState_s
{
    unsigned char shown[4]; // the lives, bombs, boosters and multiplier last drawn (SP cg+0xce514..0xce517)
    int pulseEnd[4];        // when each one's pulse ends (SP cg+0xce518..0xce524); 0 = not set up yet
    float nameAlpha;        // the names' fade (SP cg+0xce528)
};
static zombietronScoreBarState_s s_zombietronScoreBar[MAX_LOCAL_CLIENTS];
enum { ZT_LIVES, ZT_BOMBS, ZT_BOOSTERS, ZT_MULTIPLIER };

void CG_SP_ResetZombietronHud(int localClientNum)
{
    memset(&s_zombietronScoreBar[localClientNum], 0, sizeof(s_zombietronScoreBar[localClientNum]));
}

// SP's pulse when a zombietron counter goes up (inlined per column, e.g. SP 0x00892d37..0x00892db8): for a second
// the text scale gains up to 0.25, peaking half way.
static float CG_ZombietronPulse(zombietronScoreBarState_s *bar, int slot, int value, int time, float scaleDelta)
{
    if ( bar->shown[slot] != (unsigned int)value )
    {
        if ( bar->shown[slot] < (unsigned char)value )
            bar->pulseEnd[slot] = time + 1000;
        bar->shown[slot] = (unsigned char)value;
    }
    if ( time < bar->pulseEnd[slot] )
        scaleDelta += (1.0f - fabsf(500.0f - (float)(bar->pulseEnd[slot] - time)) * 0.002f) * 0.25f;
    return scaleDelta;
}

static int CG_SP_ScoreColumn(const cg_s *cgameGlob, const score_s *score, scoreboardColumnType_t columnType);

// CG_DrawPlayerScoreBar (SP 0x00892860). Five's zombiemode table, the zombietron table and the no-mode table are
// ported. Zombietron's boosters, lives, weapon meter and multiplier come from the scoreboard columns the server
// fills on zombietron (g_scr_sp_zombietron.h); SP reads them from its score_s.
static void CG_DrawPlayerScoreBar(
    int localClientNum,
    int clientNum,
    const float *baseColor,
    const score_s *score,
    float x,
    float y,
    float w,
    float h,
    int horzAlign,
    int vertAlign,
    bool isLocal)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    const ScreenPlacement *scrPlace = &scrPlaceView[localClientNum];
    const bool zombietronMode = Dvar_GetBool("zombietron");
    zombietronScoreBarState_s *bar = &s_zombietronScoreBar[localClientNum];
    const scoreBarColumn_s *columns;
    int columnCount;
    float rowHeight;
    float color[4];
    float textColor[4];
    float widthCut;
    float barX;
    float scaleDelta;
    const float barLeft = x;
    Material *material;
    Font_s *font;
    const char *materialName;

    rowHeight = (float)cg_scoreboardItemHeight->current.integer - (float)(isLocal ? 0 : 3) + (float)(zombiemode->current.enabled ? 9 : 0);
    if ( !cgameGlob->bgs.clientinfo[clientNum].infoValid )
        return;
    if ( zombietronMode )
    {
        columns = s_scoreBarColumnsZombietron;
        columnCount = 6;
    }
    else if ( zombiemode->current.enabled )
    {
        columns = s_scoreBarColumnsZombie;
        columnCount = 1;
    }
    else
    {
        columns = s_scoreBarColumnsDefault;
        columnCount = 2;
    }
    color[0] = baseColor[0];
    color[1] = baseColor[1];
    color[2] = baseColor[2];
    if ( zombietronMode )
    {
        color[3] = 1.0f;
    }
    else if ( zombiemode->current.enabled )
    {
        color[0] = (isLocal ? 0.05f : 0.0f) + color[0];
        color[3] = cg_ScoresColor_TransparencyZombie->current.value * (isLocal ? 2.0f : 0.8f) * baseColor[3];
    }
    else
    {
        color[3] = cg_ScoresColor_Transparency->current.value * baseColor[3];
    }
    // SP sets the zombietron counters up once: 3 lives, 1 bomb, 2 boosters, x1, no pulse running
    if ( zombietronMode && !bar->pulseEnd[ZT_LIVES] )
    {
        bar->shown[ZT_LIVES] = 3;
        bar->shown[ZT_BOMBS] = 1;
        bar->shown[ZT_BOOSTERS] = 2;
        bar->shown[ZT_MULTIPLIER] = 1;
        for ( int i = 0; i < 4; ++i )
            bar->pulseEnd[i] = cgameGlob->time;
    }
    widthCut = (float)(isLocal ? 0 : 20);
    if ( zombiemode->current.enabled )
    {
        if ( clientNum == 0 )
            materialName = "scorebar_zom_1";
        else if ( clientNum == 1 )
            materialName = "scorebar_zom_2";
        else if ( clientNum == 2 )
            materialName = "scorebar_zom_3";
        else
            materialName = "scorebar_zom_4";
        material = Material_RegisterHandle(materialName, 7);
        barX = x;
    }
    else
    {
        material = Material_RegisterHandle("white", 7);
        barX = widthCut + x;
    }
    UI_DrawHandlePic(scrPlace, barX, y, w - widthCut, rowHeight, horzAlign, vertAlign, color, material);
    font = UI_GetFontHandle(scrPlace, cg_scoreboardFont->current.integer, 0.35f);
    if ( zombietronMode )
    {
        textColor[0] = 1.0f;
        textColor[1] = 1.0f;
        textColor[2] = 1.0f;
    }
    else if ( zombiemode->current.enabled )
    {
        CG_ScoreBarZombieColor("cg_ScoresColor_Gamertag", clientNum, textColor);
    }
    else if ( !isLocal )
    {
        textColor[0] = score->score < 0 ? 1.0f : 0.8f;
        textColor[1] = score->score < 0 ? 0.4f : 0.8f;
        textColor[2] = score->score < 0 ? 0.4f : 0.8f;
    }
    else
    {
        textColor[0] = 1.0f;
        textColor[1] = score->score < 0 ? 0.2f : 1.0f;
        textColor[2] = score->score < 0 ? 0.2f : 1.0f;
    }
    textColor[3] = baseColor[3];
    const float baseScaleDelta = isLocal ? 0.04f : -0.05f;
    scaleDelta = baseScaleDelta;
    for ( int i = 0; i < columnCount; ++i )
    {
        float colWidth = columns[i].widthFrac * w;
        switch ( columns[i].type )
        {
        case 2:
        {
            int value = CG_ScoreBarColumnValue(clientNum, score->score);
            char *text = va("%i", value);
            if ( zombietronMode )
                CG_DrawScoreBarColumnText(localClientNum, text, font, x - 10.0f, y, colWidth, horzAlign, vertAlign, columns[i].alignment, scaleDelta + 0.45f, 3, textColor, rowHeight);
            else if ( zombiemode->current.enabled )
                CG_DrawScoreBarColumnText(localClientNum, text, font, x + 5.0f, y, colWidth, horzAlign, vertAlign, columns[i].alignment, scaleDelta + 0.35f, 3, textColor, rowHeight);
            else
                CG_DrawScoreBarColumnText(localClientNum, text, font, x, y, colWidth, horzAlign, vertAlign, columns[i].alignment, scaleDelta + 0.35f, 3, textColor, rowHeight);
            break;
        }
        case 8:  // bombs (SP score.assists) and hud_zombie_bomb
        case 9:  // boosters (SP score.downs) and hud_lightning_bolt
        case 10: // lives (SP score.revives) and stance_stand
        {
            int value;
            int slot;
            float textX;
            float iconX;
            const char *iconName;
            if ( columns[i].type == 8 )
            {
                value = score->assists;
                slot = ZT_BOMBS;
                textX = 14.0f;
                iconX = 16.0f;
                iconName = "hud_zombie_bomb";
            }
            else if ( columns[i].type == 9 )
            {
                value = CG_SP_ScoreColumn(cgameGlob, score, SB_ZT_BOOSTERS);
                slot = ZT_BOOSTERS;
                textX = 20.0f;
                iconX = 20.0f;
                iconName = "hud_lightning_bolt";
            }
            else
            {
                value = CG_SP_ScoreColumn(cgameGlob, score, SB_ZT_LIVES);
                slot = ZT_LIVES;
                textX = 10.0f;
                iconX = 11.0f;
                iconName = "stance_stand";
            }
            char *text = va("%i", value);
            if ( !zombiemode->current.enabled )
                break;
            if ( !zombietronMode ) // SP's zombiemode arm; only the zombietron table has these types
            {
                CG_DrawScoreBarColumnText(localClientNum, text, font, x + 5.0f, y, colWidth, horzAlign, vertAlign, columns[i].alignment, scaleDelta + 0.35f, 3, textColor, rowHeight);
                break;
            }
            const float pulsed = CG_ZombietronPulse(bar, slot, value, cgameGlob->time, scaleDelta);
            CG_DrawScoreBarColumnText(localClientNum, text, font, x + textX, y, colWidth, horzAlign, vertAlign, columns[i].alignment, pulsed + 0.35f, 3, textColor, rowHeight);
            UI_DrawHandlePic(scrPlace, x + iconX, y, rowHeight, rowHeight, horzAlign, vertAlign, textColor, Material_RegisterHandle(iconName, 7));
            break;
        }
        case 11: // the player's name, and above it the weapon-time meter (SP score.headshots, 0..1000) or the revive bar
        {
            if ( !zombietronMode )
                break;
            const centity_s *cent = CG_GetEntity(localClientNum, clientNum);
            const bool beingRevived = (cent->nextState.lerp.eFlags & 0x400000) != 0; // SP cent+0x168 bit 22
            const int meter = CG_SP_ScoreColumn(cgameGlob, score, SB_TYPE_HEADSHOTS);
            const float frac = (float)meter * 0.001f;
            const float meterWidth = w - 58.0f;
            float nameColor[4];
            if ( meter )
            {
                y = vertAlign == 10 ? y - 14.0f : y + 3.0f;
                if ( beingRevived )
                {
                    y += 3.0f;
                    if ( Material *bg = Material_RegisterHandle("progress_bar_bg_small", 7) )
                    {
                        static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                        CL_DrawStretchPic(scrPlace, barLeft + 5.0f, y, meterWidth, 4.0f, horzAlign, vertAlign, 0.0f, 0.0f, 1.0f, 1.0f, white, Material_RegisterHandle("progress_bar_fg_small", 7));
                        CL_DrawStretchPic(scrPlace, barLeft + 5.0f, y, meterWidth * frac, 4.0f, horzAlign, vertAlign, 0.0f, 0.0f, frac, 1.0f, white, bg);
                    }
                }
                else if ( Material *bullets = Material_RegisterHandle("hud_bullets_rifle_full", 7) )
                {
                    // the bullets redden as the weapon runs out
                    float c = frac + 0.3f;
                    if ( c > 1.0f )
                        c = 1.0f;
                    if ( c < 0.0f )
                        c = 0.0f;
                    const float bulletColor[4] = { 1.0f, c, c, 1.0f };
                    CL_DrawStretchPic(scrPlace, barLeft + 5.0f, y, meterWidth * frac, 11.0f, horzAlign, vertAlign, 0.0f, 0.0f, frac * 0.5f, 1.0f, bulletColor, bullets);
                }
                y += 2.0f;
            }
            if ( vertAlign == 10 )
                y -= 25.0f;
            nameColor[0] = textColor[0];
            nameColor[1] = textColor[1];
            nameColor[2] = textColor[2];
            // the names fade in and out with cg_drawFriendlyNames
            const float step = (float)cgameGlob->frametime * 0.0002f;
            bar->nameAlpha += cg_drawFriendlyNames->current.enabled ? step : -step;
            if ( bar->nameAlpha > 1.0f )
                bar->nameAlpha = 1.0f;
            if ( bar->nameAlpha < 0.0f )
                bar->nameAlpha = 0.0f;
            nameColor[3] = bar->nameAlpha;
            // NOT PORTED: the voice icon after the name (voice_off muted / voice_on talking, SP 0x004bc2f0 /
            // 0x0059e090), drawn only in onlinegame or systemlink games, which the port does not run.
            char name[64];
            I_strncpyz(name, BG_DisplayName(&cgameGlob->bgs.clientinfo[clientNum], 1), sizeof(name)); // SP 0x0069f600
            if ( name[0] )
            {
                if ( beingRevived && meter )
                {
                    I_strncat(name, sizeof(name), " ");
                    I_strncat(name, sizeof(name), SEH_LocalizeTextMessage("GAME_REVIVING", "game message", LOCMSG_SAFE));
                    nameColor[3] = 1.0f;
                }
                CG_DrawScoreBarColumnText(localClientNum, name, font, barLeft + 5.0f, y, w, horzAlign, vertAlign, columns[i].alignment, scaleDelta + 0.35f, 3, nameColor, rowHeight);
            }
            break;
        }
        case 12: // the score multiplier: (bar progress << 4) | (multiplier - 1), the bar 512 steps long
        {
            if ( !zombietronMode )
                break; // NOT PORTED: the no-mode table's coloured multiplier text (SP 0x00893b46..)
            const int packed = CG_SP_ScoreColumn(cgameGlob, score, SB_ZT_MULTIPLIER);
            const float fill = (float)(packed >> 4) * 0.001953125f;
            const int multiplier = (packed & 0xF) + 1;
            const float barWidth = w - 58.0f;
            y = vertAlign == 10 ? y - 4.0f : y + h;
            if ( Material *bg = Material_RegisterHandle("progress_bar_bg_small", 7) )
            {
                static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                const float barY = y - 2.0f;
                CL_DrawStretchPic(scrPlace, barLeft + 5.0f, barY, barWidth, 4.0f, horzAlign, vertAlign, 0.0f, 0.0f, 1.0f, 1.0f, white, Material_RegisterHandle("progress_bar_fg_small", 7));
                CL_DrawStretchPic(scrPlace, barLeft + 5.0f, barY, barWidth * fill, 4.0f, horzAlign, vertAlign, 0.0f, 0.0f, fill, 1.0f, white, bg);
            }
            const float pulsed = CG_ZombietronPulse(bar, ZT_MULTIPLIER, multiplier, cgameGlob->time, scaleDelta);
            CG_DrawScoreBarColumnText(localClientNum, va("x%i", multiplier), font, x - 30.0f, y - 15.0f, 25.0f, horzAlign, vertAlign, columns[i].alignment, pulsed + 0.35f, 3, textColor, rowHeight);
            break;
        }
        default:
            // NOT PORTED: column type 12 of the no-mode table (SP 0x00892860's other switch arms). Five's
            // zombiemode table has only type 2.
            break;
        }
        scaleDelta = baseScaleDelta;
        x = colWidth + x;
    }
}

// CG_OwnerDraw_ZombietronScores (SP 0x0041fb80), ownerdraw 354 of menu zombietronscores: a score bar per player in
// the screen's corners (client info slots 0..3: top left, top right, bottom left, bottom right), each in its
// player's colour, and the hud_firstplace badge on the leader's bar when more than one player plays.
bool CG_SP_OwnerDraw_ZombietronScores(int localClientNum, const rectDef_s *rect)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    const ScreenPlacement *scrPlace = &scrPlaceView[localClientNum];
    const int numClients = cgameGlob->snap->numClients;
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    if ( numClients <= 0 || !Dvar_GetInt("cl_scoreDraw") || cl_paused->current.integer )
        return false;
    // the leader; a tie for the lead names none
    int leader = -1;
    int best = 0;
    for ( int i = 0; i < 4; ++i )
    {
        const clientInfo_t *ci = &cgameGlob->bgs.clientinfo[i];
        if ( !ci->infoValid || ci->score.score < best )
            continue;
        leader = ci->score.score == best ? -1 : ci->clientNum;
        best = ci->score.score;
    }
    for ( int slot = 0; slot < 4; ++slot )
    {
        const clientInfo_t *ci = &cgameGlob->bgs.clientinfo[slot];
        if ( !ci->infoValid )
            continue;
        float color[4];
        switch ( ci->clientNum )
        {
        case 0:
            color[0] = 0.35f, color[1] = 1.0f, color[2] = 0.35f;
            break;
        case 1:
            color[0] = 0.37f, color[1] = 0.37f, color[2] = 1.0f;
            break;
        case 2:
            color[0] = 1.0f, color[1] = 0.1f, color[2] = 0.1f;
            break;
        default:
            color[0] = 0.7f, color[1] = 0.7f, color[2] = 0.2f;
            break;
        }
        color[3] = 1.0f;
        float x = 5.0f;
        float y = 5.0f;
        int horzAlign = 8;
        int vertAlign = 8;
        if ( slot == 1 || slot == 3 )
        {
            x = -rect->w;
            horzAlign = 10;
        }
        if ( slot == 2 || slot == 3 )
        {
            y = -rect->h;
            vertAlign = 10;
        }
        CG_DrawPlayerScoreBar(localClientNum, ci->clientNum, color, &ci->score, x, y, rect->w, rect->h, horzAlign, vertAlign, false);
        Material *badge = Material_RegisterHandle("hud_firstplace", 7);
        if ( badge && ci->clientNum == leader && numClients > 1 )
        {
            if ( horzAlign == 8 )
                x = rect->w - 20.0f + x;
            else
                x = x - 45.0f;
            if ( vertAlign == 10 )
                y = y - 12.0f;
            UI_DrawHandlePic(scrPlace, x, y, 45.0f, 45.0f, horzAlign, vertAlign, white, badge);
        }
    }
    return true;
}

// CG_DrawCompetitiveModeScores (SP 0x00893e50; the item rect in EAX)
static void CG_DrawCompetitiveModeScores(int localClientNum, const rectDef_s *rect, const float *itemColor)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    float y = rect->y;
    int rowCount = cgameGlob->snap->numClients;
    float color[4];

    if ( rowCount < 1 )
        return;
    if ( zombiemode->current.enabled )
    {
        scoreBarRow_s *row = s_scoreBarRows;
        for ( int i = 0; i < com_maxclients->current.integer; ++i )
        {
            if ( cgameGlob->bgs.clientinfo[i].infoValid )
            {
                row->clientNum = i;
                ++row;
            }
        }
    }
    else
    {
        // NOT PORTED: SP 0x00890c10 sorts the rows by score outside zombiemode. Five is zombiemode.
        return;
    }
    for ( int r = rowCount - 1; r > -1; --r )
    {
        int clientNum = s_scoreBarRows[r].clientNum;
        if ( !cgameGlob->bgs.clientinfo[clientNum].infoValid )
            continue;
        bool isLocal = clientNum == cgameGlob->clientNum;
        Dvar_GetUnpackedColor(cg_ScoresColor_Zombie, color);
        color[3] = itemColor[3];
        CG_DrawPlayerScoreBar(
            localClientNum,
            clientNum,
            color,
            &cgameGlob->bgs.clientinfo[clientNum].score,
            rect->x,
            y - (float)((zombiemode->current.enabled ? 9 : 0) + (isLocal ? 3 : 0)),
            rect->w,
            rect->h,
            rect->horzAlign,
            rect->vertAlign,
            isLocal);
        // MEASURED: SP sets the next row's y from xmm0 after CG_DrawPlayerScoreBar returns (an LTCG
        // register value the decompile does not show), minus (1 - (zombiemode ? 4 : 0)). Five solo has
        // one row, so only the row step for co-op is unknown; here each row steps up by the row height.
        y = y - ((float)cg_scoreboardItemHeight->current.integer + (float)(zombiemode->current.enabled ? 9 : 0))
            - (float)(1 - (zombiemode->current.enabled ? 4 : 0));
    }
}

// ownerdraw 288, CG_OwnerDraw_MiniScoreboard_288 (SP 0x005bfed0): the zombie points.
bool CG_SP_OwnerDraw_MiniScoreboard(int localClientNum, const rectDef_s *rect)
{
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    if ( cl_paused->current.integer )
        return false;
    CG_DrawCompetitiveModeScores(localClientNum, rect, white);
    return true;
}

// ---- SP scoreboard (CG_DrawScoreboard SP 0x0042f270 and what it calls). Recovered from the read-only SP
// exe with dumpbin (no decompile exists for these); constants are the exe's .rdata values. SP 0x00674400
// (called between them) is `xor al,al; ret`, so every branch it guards (splitscreen width, the 0.472 header
// scale) is dead in SP and is not ported.

// SP .rdata 0x00a60400 / 0x00a605d0: the zombiemode column tables (type, widthFrac, header, alignment).
// Types: 0 name, 2 points, 3 ping, 4 status icon, 5 talking icon, 6 kills, 7 rank icon, 8 assists, 9 downs,
// 10 revives, 11 headshots (the switch of SP 0x00891a00, jump table 0x008921b8).
static const scoreBarColumn_s s_sbColumnsZombieSolo[4] = {
    { 0, 0.625f, "", 0 }, { 2, 0.125f, "CGAME_SB_POINTS", 1 }, { 6, 0.125f, "CGAME_SB_KILLS", 1 },
    { 11, 0.125f, "CGAME_SB_HEADSHOTS", 1 } };                                                       // 0x00a60400
static const scoreBarColumn_s s_sbColumnsZombieOnline[8] = {
    { 0, 0.35f, "", 0 }, { 5, 0.05f, "", 0 }, { 2, 0.1f, "CGAME_SB_POINTS", 1 }, { 6, 0.1f, "CGAME_SB_KILLS", 1 },
    { 9, 0.1f, "CGAME_SB_DOWNS", 1 }, { 10, 0.1f, "CGAME_SB_REVIVES", 1 }, { 11, 0.1f, "CGAME_SB_HEADSHOTS", 1 },
    { 3, 0.1f, "CGAME_SB_PING", 2 } };                                                             // 0x00a605d0

// the backdrop width, inlined in SP at every use (e.g. 0x00890b37..0x00890ba2)
static float CG_SP_BackdropWidth()
{
    if ( Dvar_GetBool("zombietron") )
        return cg_scoreboardWidth->current.value * 0.9f;
    return cg_scoreboardWidth->current.value;
}

// CG_BackdropLeft (SP 0x00890b30; localClientNum in EAX)
static float CG_SP_BackdropLeft(int localClientNum)
{
    const ScreenPlacement *scrPlace = &scrPlaceView[localClientNum];
    float left = (scrPlace->virtualViewableMax[0] - scrPlace->virtualViewableMin[0] - CG_SP_BackdropWidth()) * 0.5f;
    if ( -left >= 0.0f )
        return 0.0f;
    return left;
}

// the backdrop top, inlined in SP (e.g. 0x00890f51..0x00890f8a). MP's is (480 - h) / 2 - 15.
static float CG_SP_BackdropTop()
{
    float top = (480.0f - cg_scoreboardHeight->current.value) * 0.5f + 10.0f;
    if ( -top >= 0.0f )
        return 0.0f;
    return top;
}

// CG_GetScoreboardInfo (SP 0x00890d20; the count out in EAX, the table out in ECX). Only reached in zombiemode.
static const scoreBarColumn_s *CG_SP_GetScoreboardInfo(int *numFields)
{
    if ( Dvar_GetBool("arcademode") || !zombiemode->current.enabled || Dvar_GetBool("zombietron") )
    {
        // NOT PORTED: the arcademode (0x00a60520 / 0x00a60360), zombietron (0x00a60440) and default
        // (0x00a60480 / 0x00a602d0) tables. Five is zombiemode without zombietron.
        *numFields = 0;
        return s_sbColumnsZombieSolo;
    }
    if ( onlinegame->current.enabled || Dvar_GetBool("systemlink") )
    {
        *numFields = 8;
        return s_sbColumnsZombieOnline;
    }
    *numFields = 4;
    return s_sbColumnsZombieSolo;
}

// CG_DrawBackdropServerInfo (SP 0x00890de0): host name bottom left, server address bottom right. Unlike MP's it
// does not copy the name or apply the alt colour palette.
static void CG_SP_DrawBackdropServerInfo(int localClientNum, float alpha)
{
    const ScreenPlacement *scrPlace = &scrPlaceView[localClientNum];
    const char *hostName = CG_GetLocalClientStaticGlobals(localClientNum)->szHostName;
    const char *serverIP = CL_GetServerIPAddress();
    float color[4] = { 1.0f, 1.0f, 1.0f, alpha };
    float fontScale = 0.2f;
    Font_s *font;
    float x;
    float y;

    if ( !I_stricmp(serverIP, "0.0.0.0:0") )
        serverIP = "";
    do
    {
        font = UI_GetFontHandle(scrPlace, cg_scoreboardFont->current.integer, fontScale);
        float width = CG_SP_BackdropWidth();
        int textWidth = UI_TextWidth(serverIP, 0, font, fontScale) + UI_TextWidth(hostName, 0, font, fontScale) + 4;
        if ( width - 6.0f - 8.0f >= (float)textWidth )
            break;
        fontScale = fontScale - 0.01f;
    } while ( fontScale > 0.075f );
    y = CG_SP_BackdropTop() + cg_scoreboardHeight->current.value - 3.0f - 2.0f - 14.0f + 14.0f;
    y = y - (float)(14 - UI_TextHeight(font, fontScale)) * 0.5f;
    x = CG_SP_BackdropLeft(localClientNum) + 9.0f;
    UI_DrawText(scrPlace, (char *)hostName, 0x7FFFFFFF, font, x, y, 1, 0, fontScale, color, 3);
    x = CG_SP_BackdropLeft(localClientNum) + CG_SP_BackdropWidth() - 3.0f - 2.0f - 4.0f;
    x = x - (float)(UI_TextWidth(serverIP, 0, font, fontScale) + 4);
    UI_DrawText(scrPlace, (char *)serverIP, 0x7FFFFFFF, font, x, y, 1, 0, fontScale, color, 3);
}

// CG_DrawScoreboard_ListColumnHeaders (SP 0x00891130; localClientNum in EAX). Returns y + h + 2 (MP: + 4).
static float CG_SP_DrawScoreboard_ListColumnHeaders(int localClientNum, const float *color, float y, float h, float listWidth)
{
    const ScreenPlacement *scrPlace = &scrPlaceView[localClientNum];
    const float scale = 0.315f;
    Font_s *font = UI_GetFontHandle(scrPlace, cg_scoreboardFont->current.integer, scale);
    float x = CG_SP_BackdropLeft(localClientNum) + 9.0f;
    int fieldCount;
    const scoreBarColumn_s *info = CG_SP_GetScoreboardInfo(&fieldCount);

    for ( int i = 0; i < fieldCount; ++i )
    {
        if ( *info[i].header )
        {
            char *translation = UI_SafeTranslateString(info[i].header);
            float colWidth = info[i].widthFrac * listWidth;
            float xAdj = (colWidth - (float)UI_TextWidth(translation, 0, font, scale)) * 0.5f;
            UI_DrawText(scrPlace, translation, 0x7FFFFFFF, font, xAdj + x, y + h, 1, 0, scale, color, 3);
        }
        x = info[i].widthFrac * listWidth + x;
    }
    return y + h + 2.0f;
}

// CG_DrawScoreboard_ListBanner (SP 0x00891350). In zombiemode it draws nothing and returns y + h + 2.
// NOT PORTED: the team banner it draws outside zombiemode (SP 0x008912b0, the team icon and name).
static float CG_SP_DrawScoreboard_ListBanner(float y, float h)
{
    return y + h + 2.0f;
}

// DrawListString (SP 0x008915e0; localClientNum in EAX, text in EDI, font in ESI)
static void CG_SP_DrawListString(
    int localClientNum,
    const char *text,
    Font_s *font,
    float x,
    float y,
    float maxWidth,
    int alignment,
    float scale,
    int style,
    const float *color)
{
    const ScreenPlacement *scrPlace;
    float width;
    float dx;

    if ( !text )
        return;
    scrPlace = &scrPlaceView[localClientNum];
    while ( (float)UI_TextWidth(text, 0x7FFFFFFF, font, scale) > maxWidth )
        scale = scale - 0.025f;
    if ( scale < 0.2f )
        style = 0;
    width = (float)UI_TextWidth(text, 0x7FFFFFFF, font, scale);
    if ( alignment == 1 )
        dx = (maxWidth - width) * 0.5f;
    else if ( alignment == 2 )
        dx = maxWidth - width - 4.0f;
    else
        dx = 0.0f;
    y = ((float)R_TextHeight(font) * scale + (float)cg_scoreboardItemHeight->current.integer) * cg_scoreboardTextOffset->current.value + y;
    UI_DrawText(scrPlace, (char *)text, 0x7FFFFFFF, font, x + dx, y, 1, 0, scale, color, style);
}

// SP's score_s carries headshots at +0x30 (SP 0x008920aa). BO1Zombies's MP clientState score has no such
// field; in zombiemode the server sends them as a scoreboard column (G_PopulateMatchState). 0 when absent.
static int CG_SP_ScoreColumn(const cg_s *cgameGlob, const score_s *score, scoreboardColumnType_t columnType)
{
    for ( int i = 0; i < 4; ++i )
    {
        if ( cgameGlob->scoreboardColumnTypes[i] == columnType )
            return score->scoreboardColumns[i];
    }
    return 0;
}

// CG_DrawClientScore (SP 0x00891a00): one row. The zombiemode path is ported (bar scorebar_zom_long_N,
// gamertag colour, the column switch). Returns y + item height.
static float CG_SP_DrawClientScore(
    int localClientNum,
    int clientNum,
    const float *color,
    float y,
    const score_s *score,
    float listWidth,
    int highlight)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    const ScreenPlacement *scrPlace = &scrPlaceView[localClientNum];
    clientInfo_t *ci = &cgameGlob->bgs.clientinfo[clientNum];
    const scoreBarColumn_s *info;
    int fieldCount;
    float x;
    float h;
    float barColor[4];
    float greyColor[4];
    float textColor[4];
    Material *white;
    Material *material;
    Font_s *font;
    const char *materialName;

    if ( !ci->infoValid )
        return y;
    info = CG_SP_GetScoreboardInfo(&fieldCount);
    x = CG_SP_BackdropLeft(localClientNum) + 9.0f;
    h = (float)cg_scoreboardItemHeight->current.integer;
    white = Material_RegisterHandle("white", 7);
    barColor[0] = color[0];
    barColor[1] = color[1];
    barColor[2] = color[2];
    greyColor[0] = 0.15f;
    greyColor[1] = 0.15f;
    greyColor[2] = 0.15f;
    greyColor[3] = 0.4f;
    if ( !zombiemode->current.enabled )
        UI_DrawHandlePic(scrPlace, x, y, listWidth, h, 1, 0, greyColor, white);
    if ( Dvar_GetBool("arcademode") )
        barColor[3] = cg_ScoresColor_Transparency->current.value * color[3];
    else if ( zombiemode->current.enabled )
        barColor[3] = (float)((double)cg_ScoresColor_Transparency->current.value * (highlight ? 1.75 : 1.0) * (double)color[3]);
    else
        barColor[3] = color[3] * 0.5f;
    // NOT PORTED: the zombietron bar colours by clientNum (SP 0x00891bb5..0x00891c32). Five is not zombietron.
    if ( zombiemode->current.enabled )
    {
        if ( clientNum == 0 )
            materialName = "scorebar_zom_long_1";
        else if ( clientNum == 1 )
            materialName = "scorebar_zom_long_2";
        else if ( clientNum == 2 )
            materialName = "scorebar_zom_long_3";
        else
            materialName = "scorebar_zom_long_4";
        material = Material_RegisterHandle(materialName, 7);
    }
    else
    {
        material = white;
    }
    UI_DrawHandlePic(scrPlace, x, y, listWidth, h, 1, 0, barColor, material);
    font = UI_GetFontHandle(scrPlace, cg_scoreboardFont->current.integer, 0.35f);
    x = CG_SP_BackdropLeft(localClientNum) + 9.0f;
    if ( !Dvar_GetBool("zombietron") && zombiemode->current.enabled )
    {
        CG_ScoreBarZombieColor("cg_ScoresColor_Gamertag", clientNum, textColor);
    }
    else if ( !Dvar_GetBool("zombietron") && clientNum == cgameGlob->clientNum )
    {
        Dvar_GetUnpackedColor(cg_scoreboardMyColor, textColor);
    }
    else
    {
        textColor[0] = 1.0f;
        textColor[1] = 1.0f;
        textColor[2] = 1.0f;
    }
    textColor[3] = color[3];
    for ( int i = 0; i < fieldCount; ++i )
    {
        float colWidth = info[i].widthFrac * listWidth;
        const char *text = NULL;
        switch ( info[i].type )
        {
        case 0:
            text = BG_DisplayName(ci, 3);
            break;
        case 2:
            text = va("%i", score->score);
            break;
        case 3:
            text = va("%i", score->ping);
            break;
        case 6:
            text = va("%i", score->kills);
            break;
        case 8:
            text = va("%i", score->assists);
            break;
        case 11:
            if ( !Dvar_GetBool("zombietron") )
                text = va("%i", CG_SP_ScoreColumn(cgameGlob, score, SB_TYPE_HEADSHOTS));
            break;
        default:
            // NOT PORTED: 4 status icon, 5 talking icon (voice_on / voice_off), 7 rank icon, 9 downs (score+0x28)
            // and 10 revives (score+0x2c): only the co-op table 0x00a605d0 has them, and BO1Zombies's MP
            // clientState does not carry downs / revives.
            break;
        }
        if ( text )
            CG_SP_DrawListString(localClientNum, text, font, x, y, colWidth, info[i].alignment, 0.35f, 3, textColor);
        x = colWidth + x;
    }
    // NOT PORTED: the ping graph (SP 0x00891740) drawn when cg_scoreboardPingGraph is set; SP's default is 0.
    return (float)cg_scoreboardItemHeight->current.integer + y;
}

// CG_DrawTeamOfClientScore (SP 0x008921f0)
static float CG_SP_DrawTeamOfClientScore(int localClientNum, const float *color, float y, int team, float listWidth, int *drawLine)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    float rowColor[4];

    y = CG_SP_DrawScoreboard_ListBanner(y, (float)cg_scoreboardBannerHeight->current.integer);
    if ( zombiemode->current.enabled && !Dvar_GetBool("zombietron") )
    {
        scoreBarRow_s *row = s_scoreBarRows;
        for ( int i = 0; i < com_maxclients->current.integer; ++i )
        {
            if ( cgameGlob->bgs.clientinfo[i].infoValid )
            {
                row->clientNum = i;
                ++row;
            }
        }
    }
    else
    {
        // NOT PORTED: SP 0x00890c10 sorts the rows by score outside zombiemode.
        return y;
    }
    for ( int i = 0; i < cgameGlob->numScores; ++i )
    {
        int clientNum = s_scoreBarRows[i].clientNum;
        clientInfo_t *ci = &cgameGlob->bgs.clientinfo[clientNum];
        if ( !ci->infoValid )
            continue;
        if ( !I_stricmp(ui_gametype->current.string, "vs") && ci->team != team )
            continue;
        if ( cgameGlob->scoresOffBottom )
            continue;
        if ( (float)cg_scoreboardItemHeight->current.integer + y
            > CG_SP_BackdropTop() + cg_scoreboardHeight->current.value - 3.0f - 2.0f - 14.0f - 1.0f )
        {
            cgameGlob->scoresOffBottom = 1;
            continue;
        }
        ++*drawLine;
        if ( zombiemode->current.enabled )
            Dvar_GetUnpackedColor(cg_ScoresColor_Zombie, rowColor);
        else
            CG_ScoreBarZombieColor("cg_ScoresColor_Player", clientNum, rowColor);
        rowColor[3] = color[3];
        y = CG_SP_DrawClientScore(localClientNum, clientNum, rowColor, y, &ci->score, listWidth, 0) + 2.0f;
    }
    return y;
}

// CG_DrawScoreboard_ScoresList (SP 0x00892440). No scrollbar in SP.
static void CG_SP_DrawScoreboard_ScoresList(int localClientNum, float alpha)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    float color[4] = { 1.0f, 1.0f, 1.0f, alpha };
    float listWidth;
    float y;
    int drawLine;
    int team;

    if ( !cgameGlob->numScores )
        return;
    cgameGlob->scoresOffBottom = 0;
    listWidth = CG_SP_BackdropWidth() - 6.0f - 4.0f - 8.0f;
    y = CG_SP_BackdropTop() + 42.0f + (float)(cg_scoreboardItemHeight->current.integer + 4);
    CG_SP_DrawScoreboard_ListColumnHeaders(localClientNum, color, y, (float)cg_scoreboardBannerHeight->current.integer, listWidth);
    drawLine = 1;
    if ( I_stricmp(ui_gametype->current.string, "vs") )
    {
        CG_SP_DrawTeamOfClientScore(localClientNum, color, y, cgameGlob->bgs.clientinfo[cgameGlob->clientNum].team, listWidth, &drawLine);
        cgameGlob->scoresBottom = drawLine - 1;
        return;
    }
    if ( cgameGlob->teamPlayers[TEAM_AXIS] || cgameGlob->teamPlayers[TEAM_ALLIES] )
    {
        team = cgameGlob->bgs.clientinfo[cgameGlob->clientNum].team;
        if ( team != TEAM_AXIS && team != TEAM_ALLIES )
            team = TEAM_ALLIES;
        y = CG_SP_DrawTeamOfClientScore(localClientNum, color, y, team, listWidth, &drawLine);
        y = CG_SP_DrawTeamOfClientScore(localClientNum, color, y + 4.0f, team == TEAM_AXIS ? TEAM_ALLIES : TEAM_AXIS, listWidth, &drawLine) + 4.0f;
    }
    if ( cgameGlob->teamPlayers[TEAM_FREE] )
        y = CG_SP_DrawTeamOfClientScore(localClientNum, color, y, TEAM_FREE, listWidth, &drawLine) + 4.0f;
    // NOT PORTED: SP's last pass, team 4 (cg+0xa9b20, a fifth teamPlayers entry). BO1Zombies's teamPlayers has
    // four entries and its team 3 is the MP spectator team; only the "vs" gametype reaches this.
    cgameGlob->scoresBottom = drawLine - 1;
}

// CG_DrawScoreboard (SP 0x0042f270): drawn at intermission, in online games and always in zombiemode while
// the scores are shown or fading.
// mod: Tab zombie counter (NOT in the SP exe). Drawn with the zombies scoreboard when bo1_mod_tabinfo is 1: the two
// numbers the mod script (mods/zinfo/maps/zinfo/_zinfo.gsc) sends with SetClientDvars, zinfo_alive (get_enemy_count())
// and zinfo_left (level.zombie_total + alive). Numbers in the round number's font and scale (maps\_zombiemode
// create_chalk_hud: font "default", fontscale 32, colour (0.21, 0, 0); hudelem scale = fontScale * 0.25, see
// GetHudElemInfo), small labels, stacked in the left margin above the round number so they never cover the board.
static void CG_SP_DrawTabInfo(int localClientNum, float fade)
{
    static const dvar_s *bo1_mod_tabinfo;
    if ( !bo1_mod_tabinfo )
        bo1_mod_tabinfo = _Dvar_RegisterBool("bo1_mod_tabinfo", 0, 0, "mod: zombies alive / left this round with the scoreboard (set by mods/zinfo)");
    if ( !bo1_mod_tabinfo->current.enabled )
        return;
    const char *alive = Dvar_GetString("zinfo_alive");
    const char *left = Dvar_GetString("zinfo_left");
    if ( !alive || !*alive || !left || !*left )
        return;

    const ScreenPlacement *scrPlace = &scrPlaceView[localClientNum];
    const float room = CG_SP_BackdropLeft(localClientNum) - 8.0f; // left margin beside the scoreboard (virtual units)
    if ( room <= 0.0f )
        return;
    // chalk fontscale 32 as the client gets it: the hudelem net field keeps round((fs - 1) * 10) in 6 bits
    // (sv_msg_write.cpp MSG_FIELD_FONTSCALE), so 32 arrives as 6.4; x the default font's 0.25 (GetHudElemInfo)
    const float chalkFontScale = (float)(((int)(31.0f * 10.0f + 0.5f)) & 63) * 0.1f + 1.0f;
    float numScale = chalkFontScale * 0.25f * (float)ScrPlace_HiResGetScale();
    Font_s *numFont = UI_GetFontHandle(scrPlace, 0, numScale * scrPlace->scaleVirtualToReal[1]); // as GetHudElemInfo
    const int numWidth = UI_TextWidth(alive, 0, numFont, numScale) > UI_TextWidth(left, 0, numFont, numScale)
        ? UI_TextWidth(alive, 0, numFont, numScale) : UI_TextWidth(left, 0, numFont, numScale);
    if ( (float)numWidth > room ) // 4:3 or a huge count: shrink until it fits beside the board
        numScale = numScale * room / (float)numWidth;
    const float labelScale = 1.8f * 0.25f * (float)ScrPlace_HiResGetScale(); // retail zombie text lines: fontscale 1.8
    Font_s *labelFont = UI_GetFontHandle(scrPlace, 0, labelScale * scrPlace->scaleVirtualToReal[1]);
    const float numHeight = (float)UI_TextHeight(numFont, numScale);
    const float labelHeight = (float)UI_TextHeight(labelFont, labelScale);
    const float numColor[4] = { 0.21f, 0.0f, 0.0f, fade };
    const float labelColor[4] = { 1.0f, 1.0f, 1.0f, 0.8f * fade };

    // baselines from the bottom (vertAlign 3), above the round number (y -4; chalk marks 64 high, digits numHeight)
    float y = -4.0f - (numHeight > 64.0f ? numHeight : 64.0f) - 12.0f;
    UI_DrawText(scrPlace, (char *)left, 0x7FFFFFFF, numFont, 4.0f, y, 1, 3, numScale, numColor, 3);
    y -= numHeight + 2.0f;
    UI_DrawText(scrPlace, (char *)"left this round", 0x7FFFFFFF, labelFont, 4.0f, y, 1, 3, labelScale, labelColor, 3);
    y -= labelHeight + 8.0f;
    UI_DrawText(scrPlace, (char *)alive, 0x7FFFFFFF, numFont, 4.0f, y, 1, 3, numScale, numColor, 3);
    y -= numHeight + 2.0f;
    UI_DrawText(scrPlace, (char *)"zombies alive", 0x7FFFFFFF, labelFont, 4.0f, y, 1, 3, labelScale, labelColor, 3);
}

int CG_SP_DrawScoreboard(int localClientNum)
{
    cg_s *cgameGlob;
    float fade;

    if ( cl_paused->current.integer )
        return 0;
    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    if ( cgameGlob->snap->ps.pm_type != 5 && !onlinegame->current.enabled && !zombiemode->current.enabled )
        return 0;
    if ( cgameGlob->showScores ) // SP cg+0xa9b38
    {
        static const char *const menuNames[] = { "error_popmenu_party", "error_popmenu_lobby", "error_popmenu", NULL };
        bool clearErrors = false;

        fade = 1.0f;
        for ( int i = 0; menuNames[i]; ++i )
        {
            if ( Menu_IsMenuOpenAndVisible(localClientNum, menuNames[i]) )
            {
                UI_CloseMenu(localClientNum, menuNames[i]);
                clearErrors = true;
                Com_Printf(14, "Clearing Error %s\n", menuNames[i]);
            }
        }
        if ( clearErrors )
            UI_ClearErrors();
    }
    else
    {
        // SP cg+0x8a350 / cg+0xa9b40, as in CG_SP_DrawIntermission
        if ( cgameGlob->physicsTime - cgameGlob->scoreFadeTime < 0 )
        {
            cgameGlob->scoreFadeTime = 0;
            return 0;
        }
        const float *fadeColor = CG_FadeColor(cgameGlob->physicsTime, cgameGlob->scoreFadeTime, 100, 100);
        if ( !fadeColor )
            return 0;
        fade = *fadeColor;
    }
    CG_SP_DrawBackdropServerInfo(localClientNum, fade);
    CG_SP_DrawScoreboard_ScoresList(localClientNum, fade);
    CG_SP_DrawTabInfo(localClientNum, fade); // mod: off (bo1_mod_tabinfo 0) = retail
    return 1;
}

// DrawIntermission (SP 0x00772a30). SP's CG_Draw2DInternal (0x00655a50) calls it when pm_type is 5 and then
// goes on drawing the HUD elements and menus, which is where the scripts' end screen ("GAME OVER",
// "You survived N rounds") is drawn.
void CG_SP_DrawIntermission(int localClientNum)
{
    cg_s *cgameGlob;

    if ( cl_paused->current.integer )
        return;
    // SP compares the active menu with its own 16; BO1Zombies's MP DrawIntermission makes the same check
    // with UIMENU_SCRIPT_POPUP.
    if ( UI_GetActiveMenu(localClientNum) == UIMENU_SCRIPT_POPUP && UI_GetTopActiveMenuName(localClientNum) )
        return;
    // SP 0x004f3540: 0x004b08b0(lc), then the Menus_CloseAll / UI_SetActiveMenu(lc, 0) pair that
    // BO1Zombies's UI_CloseAll is. The first call (0x004b08b0) is not identified and not made.
    UI_CloseAll(localClientNum);
    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    cgameGlob->scoreFadeTime = cgameGlob->physicsTime; // SP cg+0xa9b40 = cg+0x8a350
    if ( !cgameGlob->showScores ) // SP 0x00415580 reads cg+0xa9b38
        Dvar_SetInt((dvar_s *)cl_paused, 0);
    CG_SP_DrawScoreboard(localClientNum);
    // mod: bo1_mod_quickrestart - the hint for the reload key (SP_QuickRestart), bottom centre of the game over screen
    if ( SP_QuickRestartDvar()->current.enabled )
    {
        const ScreenPlacement *scrPlace = &scrPlaceView[localClientNum];
        const char *hint = "Press R to restart";
        const float scale = 1.8f * 0.25f * (float)ScrPlace_HiResGetScale();
        Font_s *font = UI_GetFontHandle(scrPlace, 0, scale * scrPlace->scaleVirtualToReal[1]);
        const float color[4] = { 1.0f, 1.0f, 1.0f, 0.9f };
        UI_DrawText(scrPlace, (char *)hint, 0x7FFFFFFF, font, -0.5f * (float)UI_TextWidth(hint, 0, font, scale), -40.0f,
            2, 3, scale, color, 3);
    }
}

// zombies (p1 chunk 24): the SP screen fade. SP keeps one per local client at 0x00BF890C (24 bytes: end alpha,
// start alpha, current alpha, start time, duration, hold byte), in Sys_Milliseconds time. CL_FirstSnapshot
// (SP 0x00516F20) starts a fade-in with CG_SP_ScreenFadeIn(lc, loopback server ? 500 : 1000, 750), so the first
// frames of a level are drawn under black (fully black for <delay> ms, then fading out over <delay> + 750 ms), and
// CG_Draw2DInternal (SP 0x00655A50) draws it last (CG_SP_DrawScreenFade). BO1Zombies MP had neither.
// SP also starts the same fade-in at the end of cgame init (0x0088FC70, called at 0x0064F83E); that one ends
// behind the load screen before the first snapshot, which restarts it, so only the CL_FirstSnapshot call is ported.
// Another SP setter (0x0041EF70, fade with colour and times from its caller) is not reached by Five's first frames;
// the "fade" console command (L4, below) uses it: the pause menu's Restart Level runs "fade 0 0 0 255 0 0 1".
const dvar_s *cg_defaultFadeScreenColor; // SP 0x02FF67E8, registered at 0x004A58A9

namespace
{
struct CgSpScreenFade
{
    float endAlpha;
    float startAlpha;
    float alpha;
    int startTime;
    int duration;
    bool hold;
};
CgSpScreenFade s_cgSpScreenFade[1];
}

// SP 0x00403030
void CG_SP_ScreenFadeIn(int localClientNum, int delayMs, int fadeMs)
{
    CgSpScreenFade *fade = &s_cgSpScreenFade[localClientNum];
    fade->endAlpha = 0.0f;
    const int now = (int)Sys_Milliseconds();
    fade->startTime = now + delayMs;
    fade->duration = delayMs + fadeMs;
    fade->startAlpha = 1.0f;
    fade->alpha = 1.0f;
    fade->hold = false;
    Com_Printf(0, "Fade in %i %i\n", fade->startTime, fade->duration);
}

// zombies (L4): SP 0x0041EF70. The colour arguments are not stored (the draw uses cg_defaultFadeScreenColor); the
// alpha is 0..255 and the fade holds at start alpha 1 until startTime + duration, then shows a / 255.
void CG_SP_ScreenFade(int localClientNum, int r, int g, int b, int a, int startTime, int duration, bool hold)
{
    CgSpScreenFade *fade = &s_cgSpScreenFade[localClientNum];
    (void)r;
    (void)g;
    (void)b;
    fade->endAlpha = (float)a * (1.0f / 255.0f);
    fade->startAlpha = 1.0f;
    fade->startTime = startTime;
    fade->duration = duration;
    fade->hold = hold;
    if ( fade->duration + fade->startTime <= (int)Sys_Milliseconds() )
        fade->alpha = fade->endAlpha;
}

// zombies (L4): SP console command "fade" (registered at SP 0x00563C5E, handler 0x00770AE0):
// fade <r> <g> <b> <a> <seconds> [hold]. Needs a snapshot (SP cg+0x34) and at least 6 arguments; the time is
// seconds * 1000 from now (Sys_Milliseconds, SP 0x0062EF60); hold is set when a 7th argument exists. The pause
// menu's Restart Level (popup_restart_warning) runs "fade 0 0 0 255 0 0 1", which blacks the screen until the
// restarted level's CL_FirstSnapshot fade-in; BO1Zombies had no "fade" command.
void CG_SP_Fade_f()
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(0);
    if ( !cgameGlob->nextSnap || Cmd_Argc() < 6 )
        return;
    const int r = atoi(Cmd_Argv(1));
    const int g = atoi(Cmd_Argv(2));
    const int b = atoi(Cmd_Argv(3));
    const int a = atoi(Cmd_Argv(4));
    const int duration = 1000 * atoi(Cmd_Argv(5));
    const bool hold = Cmd_Argc() > 6 && Cmd_Argv(6) != 0;
    CG_SP_ScreenFade(0, r, g, b, a, (int)Sys_Milliseconds(), duration, hold);
}

// zombies (L4): SP console command "silence" (registered at SP 0x00563C75, handler 0x00770CA0): the level-fade
// snapshot "fadein" at once (SND_SetSnapshot SP 0x006057F0, category 10, length 0, amount 1), which mutes the level
// before Restart Level's fast_restart. BO1Zombies had no "silence" command.
void CG_SP_Silence_f()
{
    SND_SetSnapshot(SND_SNAPSHOT_LEVELFADE, "fadein", 0.0f, 1.0f);
}

// zombies (L4): SP CG_RefreshAllHudFades (0x004E31F0). SP CG_Draw2DInternal calls it every frame while the game is
// paused with cg_drawpaused set (0x00655CF6..0x00655D0E), so the faded HUD groups (health, ammo, compass, stance,
// sprint meter, offhand, objectives) are shown under the pause menu. Each group's fade time is moved up to the
// (frozen) cg time, as CG_MenuShowNotify does for one group. SP's time is cg+0x8A350 (KB physicsTime); the fade
// fields cg+0xA9D78..0xA9D8C are KB's compass/health/ammo/stance/sprint/offhandFadeTime in the same order; SP's
// objectiveinfo field (cg+0xBB21C) is KB's scoreFadeTime, the one KB's CG_MenuShowNotify case 6 uses for it.
// "heatinfo" is shown every call, as in SP. The MP exe has no such refresh.
void CG_SP_RefreshAllHudFades(int localClientNum)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    const int time = cgameGlob->physicsTime;
    if ( cgameGlob->healthFadeTime < time )
    {
        cgameGlob->healthFadeTime = time;
        Menus_ShowByName(&cgDC[localClientNum], "Health");
    }
    if ( cgameGlob->ammoFadeTime < time )
    {
        cgameGlob->ammoFadeTime = time;
        Menus_ShowByName(&cgDC[localClientNum], Dvar_GetBool("zombiemode") ? "weaponinfozombies" : "weaponinfo");
        Menus_ShowByName(&cgDC[localClientNum], "weaponinfo_lowdef");
    }
    Menus_ShowByName(&cgDC[localClientNum], "heatinfo");
    if ( cgameGlob->compassFadeTime < time )
    {
        cgameGlob->compassFadeTime = time;
        Menus_ShowByName(&cgDC[localClientNum], "Compass");
    }
    if ( cgameGlob->stanceFadeTime < time )
    {
        cgameGlob->stanceFadeTime = time;
        Menus_ShowByName(&cgDC[localClientNum], "stance");
    }
    if ( cgameGlob->sprintFadeTime < time )
    {
        cgameGlob->sprintFadeTime = time;
        Menus_ShowByName(&cgDC[localClientNum], "sprintMeter");
    }
    if ( cgameGlob->offhandFadeTime < time )
    {
        cgameGlob->offhandFadeTime = time;
        Menus_ShowByName(&cgDC[localClientNum], "offhandinfo");
    }
    if ( cgameGlob->scoreFadeTime < time )
    {
        cgameGlob->scoreFadeTime = time;
        Menus_ShowByName(&cgDC[localClientNum], "objectiveinfo");
    }
}

// SP 0x00771D20
void CG_SP_DrawScreenFade(int localClientNum)
{
    CgSpScreenFade *fade = &s_cgSpScreenFade[localClientNum];
    const int now = (int)Sys_Milliseconds();
    if ( fade->startTime > now )
    {
        fade->alpha = fade->startAlpha;
    }
    else
    {
        const int endTime = fade->duration + fade->startTime;
        if ( endTime < now )
            fade->alpha = fade->endAlpha;
        else if ( fade->hold )
            fade->alpha = fade->startAlpha;
        else
            fade->alpha = (float)(now - fade->startTime) / (float)fade->duration * (fade->endAlpha - fade->startAlpha)
                + fade->startAlpha;
    }
    if ( fade->alpha <= 0.0f )
        return;
    float color[4];
    color[0] = (float)cg_defaultFadeScreenColor->current.color[0] * (1.0f / 255.0f);
    color[1] = (float)cg_defaultFadeScreenColor->current.color[1] * (1.0f / 255.0f);
    color[2] = (float)cg_defaultFadeScreenColor->current.color[2] * (1.0f / 255.0f);
    color[3] = fade->alpha - 1.0f < 0.0f ? fade->alpha : 1.0f;
    int width, height;
    float aspect;
    CL_GetScreenDimensions(&width, &height, &aspect);
    UI_FillRectPhysical(0.0f, 0.0f, (float)width, (float)height, color);
}

// ---- SP 3D objective markers: ownerdraw 322 of menu objective_indicators (CG_DrawObjectiveIndicators, SP
// 0x00643260). The objectives come from the snapshot's playerState (KB's transport); SP parses them from its
// objective configstrings. Objective_Set3D (g_scr_sp_zombietron.cpp) sets use3D, the colour index and the text.

// SP registers these in CG_RegisterDvars (SP 0x004a5eb7..0x004a6589); defaults and bounds are the exe's.
static const dvar_t *hud_showObjectives;
static const dvar_t *cg_objectiveIndicatorColor;
static const dvar_t *cg_objectiveIndicatorNearFadeDist;
static const dvar_t *cg_objectiveIndicatorFarFadeDist;
static const dvar_t *cg_objectiveIndicatorNearDist;
static const dvar_t *cg_objectiveIndicatorFarDist;
static const dvar_t *cg_objectiveIndicatorFarScale;
static const dvar_t *cg_objectiveIndicatorSize;

void CG_SP_RegisterObjectiveIndicatorDvars()
{
    cg_objectiveIndicatorColor = _Dvar_RegisterColor("cg_objectiveIndicatorColor", 1.0f, 0.94f, 0.0f, 1.0f, 0x80u, "");
    cg_objectiveIndicatorNearFadeDist = _Dvar_RegisterFloat("cg_objectiveIndicatorNearFadeDist", 256.0f, 0.0f, FLT_MAX, 0x1000u, "");
    cg_objectiveIndicatorFarFadeDist = _Dvar_RegisterFloat("cg_objectiveIndicatorFarFadeDist", 8192.0f, 0.0f, FLT_MAX, 0x1000u, "");
    cg_objectiveIndicatorNearDist = _Dvar_RegisterFloat("cg_objectiveIndicatorNearDist", 256.0f, 0.0f, FLT_MAX, 0x1000u, "");
    cg_objectiveIndicatorFarDist = _Dvar_RegisterFloat("cg_objectiveIndicatorFarDist", 512.0f, 0.0f, FLT_MAX, 0x1000u, "");
    cg_objectiveIndicatorFarScale = _Dvar_RegisterFloat("cg_objectiveIndicatorFarScale", 0.5f, 0.0f, FLT_MAX, 0x1000u, "");
    cg_objectiveIndicatorSize = _Dvar_RegisterFloat("cg_objectiveIndicatorSize", 0.8f, 0.0f, 100.0f, 0x1000u, "");
    hud_showObjectives = _Dvar_RegisterBool("hud_showObjectives", 1, 0x1000u, "");
}

void CG_SP_DrawObjectiveIndicators(int localClientNum)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    const ScreenPlacement *scrPlace = &scrPlaceView[localClientNum];

    if ( (Dvar_GetBool("zombietron") && localClientNum) || !hud_showObjectives->current.enabled || !cgameGlob->nextSnap )
        return;
    Font_s *font = UI_GetFontHandle(scrPlace, 1, 1.0f);
    const float scaleX = scrPlace->scaleVirtualToReal[0];
    const float scaleY = scrPlace->scaleVirtualToReal[1];
    const float scaleAvg = (scaleX + scaleY) * 0.5f;
    const float pointerWidth = waypointOffscreenPointerWidth->current.value * scaleX;
    float alphaThreshold = waypointOffscreenDistanceThresholdAlpha->current.value * scaleAvg;
    if ( alphaThreshold < 0.1f )
        alphaThreshold = 0.1f;
    const float pointerDistance = waypointOffscreenPointerDistance->current.value * scaleAvg;
    const float pointerHeight = waypointOffscreenPointerHeight->current.value * scaleY;
    const float pad = pointerHeight * 0.5f + pointerDistance;
    const float padLeft = waypointOffscreenPadLeft->current.value * scaleX + pad;
    const float padRight = waypointOffscreenPadRight->current.value * scaleX + pad;
    const float padTop = waypointOffscreenPadTop->current.value * scaleY + pad;
    const float padBottom = waypointOffscreenPadBottom->current.value * scaleY + pad;

    for ( int objNum = 0; objNum < 32; ++objNum )
    {
        const objective_t *obj = &cgameGlob->nextSnap->ps.objective[objNum];
        if ( !(obj->use3D & 1) )
            continue;
        // NOT PORTED: markers bound to a UI3D window (use3D bits 1..4, SP 0x006e1cb0 / 0x006e1cf0 and the
        // extra-cam projection 0x0067f2d0); DOA has none, so they are not drawn.
        if ( ((obj->use3D >> 1) & 0xF) )
            continue;
        if ( obj->state != OBJST_ACTIVE && obj->state != OBJST_CURRENT )
            continue;
        float worldPos[3];
        if ( obj->entNum == ENTITYNUM_NONE )
        {
            if ( obj->origin[0] == 0.0f && obj->origin[1] == 0.0f && obj->origin[2] == 0.0f )
                continue;
            Vec3Copy(obj->origin, worldPos); // + SP's offset (offx / offy / offz), not ported: 0
        }
        else
        {
            const centity_s *cent = CG_GetEntity(localClientNum, obj->entNum);
            Vec3Copy(cent->pose.origin, worldPos);
            // SP raises markers on players and actors (SP eTypes 1 and 0x10) by stance
            if ( cent->nextState.eType == ET_PLAYER || cent->nextState.eType == ET_ACTOR )
            {
                if ( cent->nextState.lerp.eFlags & 8 )
                    worldPos[2] += waypointPlayerOffsetProne->current.value;
                else if ( cent->nextState.lerp.eFlags & 4 )
                    worldPos[2] += waypointPlayerOffsetCrouch->current.value;
                else
                    worldPos[2] += waypointPlayerOffsetStand->current.value;
            }
            if ( worldPos[0] == 0.0f && worldPos[1] == 0.0f && worldPos[2] == 0.0f )
                continue;
        }
        float arrowColor[4];
        switch ( obj->color )
        {
        case 1:
            arrowColor[0] = 1.0f, arrowColor[1] = 1.0f, arrowColor[2] = 1.0f, arrowColor[3] = 1.0f;
            break;
        case 2:
            arrowColor[0] = 1.0f, arrowColor[1] = 0.0f, arrowColor[2] = 0.0f, arrowColor[3] = 1.0f;
            break;
        case 3:
            arrowColor[0] = 0.0f, arrowColor[1] = 1.0f, arrowColor[2] = 0.0f, arrowColor[3] = 1.0f;
            break;
        case 4:
            arrowColor[0] = 0.0f, arrowColor[1] = 0.0f, arrowColor[2] = 1.0f, arrowColor[3] = 1.0f;
            break;
        case 5: // SP .rdata 0x009baf28 / 0x009f1140 / 0x009e1618
            arrowColor[0] = 0.878f, arrowColor[1] = 0.95f, arrowColor[2] = 0.9f, arrowColor[3] = 1.0f;
            break;
        default:
            Dvar_GetUnpackedColor(cg_objectiveIndicatorColor, arrowColor);
            break;
        }
        float screenPos[3];
        WorldPosToScreenPos(localClientNum, worldPos, screenPos);
        float clampedDir[2];
        float clampedDist;
        const bool clamped = ClampScreenPosToEdges(localClientNum, screenPos, padLeft, padRight, padTop, padBottom, clampedDir, &clampedDist);
        const float distance = Vec3Distance(worldPos, cgameGlob->refdef.vieworg);
        if ( clamped && clampedDist > 0.0f )
        {
            // off screen: an arrow at the screen's edge, in the objective's colour, pointing at it
            if ( clampedDist < alphaThreshold )
                arrowColor[3] = arrowColor[3] / alphaThreshold * clampedDist;
            const float arrowX = clampedDir[0] * pointerDistance + screenPos[0];
            const float arrowY = clampedDir[1] * pointerDistance + screenPos[1];
            const float angle = (float)atan2(clampedDir[0], -clampedDir[1]) * 57.295776f;
            CL_DrawStretchPicPhysicalRotateXYW(arrowX - pointerWidth * 0.5f, arrowY - pointerHeight * 0.5f, distance,
                pointerWidth, pointerHeight, 0.0f, 0.0f, 1.0f, 1.0f, angle, arrowColor, cgMedia.offscreenObjectivePointer);
            continue;
        }
        // on screen: the marker and its text (the distance in metres when there is none; "*" draws nothing)
        char distanceText[32];
        const char *text;
        const int textIndex = G_SP_GetObjective3DText(obj);
        if ( textIndex )
        {
            text = SEH_LocalizeTextMessage(CL_GetConfigString(CS_LOCALIZED_STRINGS + textIndex), "game message", LOCMSG_SAFE);
            if ( *text == '*' )
                continue;
        }
        else
        {
            const float metres = distance * 0.0254f;
            Com_sprintf(distanceText, sizeof(distanceText), metres >= 10.0f ? "%0.0fm" : "%0.1fm", metres);
            text = distanceText;
        }
        float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        if ( distance < cg_objectiveIndicatorNearFadeDist->current.value )
            color[3] = 1.0f - (cg_objectiveIndicatorNearDist->current.value - distance) / cg_objectiveIndicatorNearDist->current.value;
        else if ( distance > cg_objectiveIndicatorFarFadeDist->current.value )
            color[3] = 1.0f - (distance - cg_objectiveIndicatorFarFadeDist->current.value) / cg_objectiveIndicatorNearFadeDist->current.value;
        float sizeScale = 1.0f;
        if ( distance >= cg_objectiveIndicatorNearDist->current.value )
        {
            float frac = (distance - cg_objectiveIndicatorNearDist->current.value)
                / (cg_objectiveIndicatorFarDist->current.value - cg_objectiveIndicatorNearDist->current.value);
            if ( frac > 1.0f )
                frac = 1.0f;
            if ( frac < 0.0f )
                frac = 0.0f;
            sizeScale = cg_objectiveIndicatorFarScale->current.value * frac + (1.0f - frac);
        }
        // SP draws 1080-line screens with a narrower, larger marker (SP compares the video height, the word at
        // 0x03e5e69a, with 1080)
        float markerAspect = 1.0f;
        float markerScale = 1.0f;
        if ( cls.vidConfig.displayHeight == 1080 )
        {
            markerAspect = 0.6666667f;
            markerScale = 1.5f;
        }
        const float yScale = (float)R_NormalizedTextScale(font, cg_objectiveIndicatorSize->current.value * sizeScale) * markerScale;
        const float xScale = yScale * scrPlace->scaleRealToVirtual[1] * scrPlace->scaleVirtualToReal[0];
        CL_DrawStretchPicPhysicalRotateXYW(screenPos[0] - 7.5f, screenPos[1], distance, markerScale * markerAspect * 15.0f,
            markerScale * 15.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, color, Material_RegisterHandle("hud_onscreenobjectivepointer", 7) /* SP media 0x02ff65d0 */);
        const int width = R_TextWidth(text, 0x7FFFFFFF, font);
        R_AddCmdDrawTextW((char *)text, 0x7FFFFFFF, font, screenPos[0] - (float)width * xScale * 0.5f, screenPos[1],
            distance, xScale, yScale, 0.0f, color, 3);
    }
}
