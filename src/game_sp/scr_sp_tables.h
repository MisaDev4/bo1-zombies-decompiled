#pragma once
// SP builtin tables: the builtins Black Ops SP (zombies) scripts need that BO1Zombies's MP tables lack
// (P2 of notes/game-gap.md, "the compile gate").
//
// A builtin lookup searches, first match wins:
//   1. BO1Zombies's own tables - server: g_scr_main_mp.cpp `functions` / `methods_3` and the per-class
//      method tables (Player_, ScriptEnt_, ScriptVehicle_, HudElem_, Helicopter_, Actor_GetMethod);
//      client: cg_scr_main.cpp `client_functions` / `client_methods`, cg_scr_main_mp.cpp
//      `client_project_*`. A real KB builtin always wins.
//   2. the SP lane tables listed below, in list order;
//   3. the generated not-ported table (tools/gen_sp_builtins.py): one loud Scr_Error per name. Keep it last.
//
// Adding a lane: one X(...) line below plus a .cpp that includes this header and defines
// <prefix>_functions[], <prefix>_function_count, <prefix>_methods[], <prefix>_method_count
// (copy src/game_sp/g_scr_sp_players.cpp). After adding a real builtin to a lane table, rerun
// `python tools/gen_sp_builtins.py` so its not-ported stub disappears (it parses the lane tables).

#include <cgame/cg_scr_main.h>

#define SCR_SP_SERVER_TABLES(X) \
    X(g_sp_players)    /* lane C: players / session      src/game_sp/g_scr_sp_players.cpp */ \
    X(g_sp_anim)       /* lane D: anim family, notetracks src/game_sp/g_scr_sp_anim.cpp */ \
    X(g_sp_entity)     /* lane E: entity / misc          src/game_sp/g_scr_sp_entity.cpp */ \
    X(g_sp_ai)         /* lane G: AI builtins            src/game_sp/g_scr_sp_ai.cpp */ \
    X(g_sp_zombietron) /* lane Z: Dead Ops Arcade        src/game_sp/g_scr_sp_zombietron.cpp */ \
    X(g_sp_notported)  /* GENERATED, keep last           src/game_sp/g_scr_sp_notported.cpp */

#define SCR_SP_CLIENT_TABLES(X) \
    X(g_csp_client)    /* lane F: client builtins        src/cgame/cg_scr_sp_client.cpp */ \
    X(g_csp_notported) /* GENERATED, keep last           src/cgame/cg_scr_sp_notported.cpp */

// Each table ends with a { nullptr, nullptr, 0 } row (C++ has no empty arrays); the counts exclude it.
#define SCR_SP_DECLARE_TABLE(prefix) \
    extern const BuiltinFunctionDef prefix##_functions[]; \
    extern const unsigned int prefix##_function_count; \
    extern const BuiltinMethodDef prefix##_methods[]; \
    extern const unsigned int prefix##_method_count;

SCR_SP_SERVER_TABLES(SCR_SP_DECLARE_TABLE)
SCR_SP_CLIENT_TABLES(SCR_SP_DECLARE_TABLE)

// Called by Scr_GetFunction / Scr_GetMethod (g_scr_main_mp.cpp) and CScr_GetFunction / CScr_GetMethod
// (cg_scr_main.cpp) after KB's own tables. Same contract as those: on a match, *pName is replaced by the
// table's string and *type is set to the row's developer flag; return 0 if no table has the name.
void (__cdecl *__cdecl Scr_SP_GetFunction(const char **pName, int *type))();
void (__cdecl *__cdecl Scr_SP_GetMethod(const char **pName, int *type))(scr_entref_t);
void (__cdecl *__cdecl CScr_SP_GetFunction(const char **pName, int *type))();
void (__cdecl *__cdecl CScr_SP_GetMethod(const char **pName, int *type))(scr_entref_t);
