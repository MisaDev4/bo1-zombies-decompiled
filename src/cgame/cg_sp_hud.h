#pragma once
// zombies: SP client HUD pieces (cg_sp_hud.cpp). Called only in zombiemode, except the dvar registration.
struct rectDef_s;

void CG_SP_RegisterScoreBarDvars();
void CG_SP_SetupScoreboardDvars();
bool CG_SP_OwnerDraw_MiniScoreboard(int localClientNum, const rectDef_s *rect);
bool CG_SP_OwnerDraw_ZombietronScores(int localClientNum, const rectDef_s *rect); // ownerdraw 354
void CG_SP_ResetZombietronHud(int localClientNum);
void CG_SP_RegisterObjectiveIndicatorDvars();
void CG_SP_DrawObjectiveIndicators(int localClientNum); // ownerdraw 322
int CG_SP_DrawScoreboard(int localClientNum);
void CG_SP_DrawIntermission(int localClientNum);
extern const struct dvar_s *cg_defaultFadeScreenColor; // SP 0x02FF67E8
void CG_SP_ScreenFadeIn(int localClientNum, int delayMs, int fadeMs); // SP 0x00403030
void CG_SP_DrawScreenFade(int localClientNum); // SP 0x00771D20
void CG_SP_ScreenFade(int localClientNum, int r, int g, int b, int a, int startTime, int duration, bool hold); // SP 0x0041EF70
void CG_SP_Fade_f(); // SP 0x00770AE0, console command "fade"
void CG_SP_Silence_f(); // SP 0x00770CA0, console command "silence"
void CG_SP_RefreshAllHudFades(int localClientNum); // SP 0x004E31F0
