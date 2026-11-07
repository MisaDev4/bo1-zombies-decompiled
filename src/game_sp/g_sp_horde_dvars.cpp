// mod: descriptions for the horde mod's dvars (mods/horde/README.md has the long version).
// The scripts create their dvars with SetDvar, which gives them no description in the console. Registering them here, at
// startup, gives each a type, a range and a description. The default of each one is what the script sets when its dvar is
// still empty ( if ( GetDvar( ... ) == "" ) SetDvar( ... ) ), so a game started without horde behaves the same: the dvars
// exist but nothing reads them. A value set with +set on the command line is kept when the dvar is registered.
// Not here: the horde_enemy_* dvars. mods/nightmare fills them in per difficulty only while they are empty.
#include "g_sp_horde_dvars.h"
#include <universal/dvar.h>

void G_SP_RegisterHordeDvars()
{
    // rounds and size of the horde
    _Dvar_RegisterInt("horde_max_zombies", 24, 1, 2000, 0,
        "horde mod: zombies alive at once (retail 24); read every half second, clamped to bo1_mod_maxactors");
    _Dvar_RegisterInt("horde_start_round", 0, 0, 100000, 0,
        "horde mod: start on this round, with its zombie speed and spawn delay (0 or 1 = round 1); command line only");
    _Dvar_RegisterInt("horde_carryover", 1, 0, 1, 0,
        "horde mod: 1 = the next round starts when the last zombie has spawned, the rest carry over; 0 = also wait until all are dead (retail)");
    _Dvar_RegisterFloat("horde_fill_rate", -1.0f, -1.0f, 10000.0f, 0,
        "horde mod: extra zombie spawns per second while below the limit (-1 = horde_max_zombies / 60, 0 = retail spawning only)");
    _Dvar_RegisterInt("horde_spawn_all", 0, 0, 1, 0,
        "horde mod: 1 = zombies spawn from every window of every opened area that connects to a player, not only nearby ones");
    _Dvar_RegisterInt("horde_hud", 0, 0, 1, 0,
        "horde mod: 1 = show the live zombie count at the top left");

    // drops
    _Dvar_RegisterInt("horde_perk_drop", 15, 0, 100, 0,
        "horde mod: percent of the zombie power-up drops that are a free-perk bottle (100 = all of them, 0 = off, retail); approximate between; ignored with noperks");

    _Dvar_RegisterInt("horde_drop_max", -1, -1, 1000, 0,
        "horde mod: most power-up drops per round (retail 4); 0 = none, -1 = retail");
    _Dvar_RegisterInt("horde_drop_step", -1, -1, 10000000, 0,
        "horde mod: team score between power-up drops, the first gap (retail 2000); -1 = retail");
    _Dvar_RegisterFloat("horde_drop_growth", -1.0f, -1.0f, 100.0f, 0,
        "horde mod: what the score gap between drops is multiplied by after each drop (retail 1.14; 1 = always the same gap); -1 = retail");

    // big crowds
    _Dvar_RegisterInt("horde_unjam", 1, 0, 1, 0,
        "horde mod: 1 = a zombie stuck in the crowd walks through the others for 1.5 s; 0 = retail steering");
    _Dvar_RegisterInt("horde_unjam_log", 1, 0, 2, 0,
        "horde mod: log horde_uj lines every 5 s (0 = off, 2 = also list where stuck zombies are)");
    _Dvar_RegisterInt("horde_unjam_radius", 250, 0, 100000, 0,
        "horde mod: a zombie walks through others only while at least this far from every player (units)");
    _Dvar_RegisterInt("horde_unjam_exit", 2, 0, 2, 0,
        "horde mod: how a zombie that walks through others gets its collision back near a player (1 or 2; see the horde README)");
    _Dvar_RegisterInt("horde_window_fix", 1, 0, 3, 0,
        "horde mod: bits for big hordes at windows: 1 = walk through the crowd to a window spot, 2 = script helpers (3 = both, 0 = retail); only above 24 zombies");
    _Dvar_RegisterInt("horde_window_log", 1, 0, 2, 0,
        "horde mod: log horde_win lines every 5 s (0 = off, 2 = also list where stuck zombies are)");

    // test aids
    _Dvar_RegisterInt("horde_tough", 0, 0, 1, 0,
        "horde mod, test aid: 1 = players get 100000 health, refilled every half second");
    _Dvar_RegisterInt("horde_open_doors", 0, 0, 1, 0,
        "horde mod, test aid: 1 = open every door, airlock and debris at the start, free");
    _Dvar_RegisterInt("horde_power_on", 0, 0, 1, 0,
        "horde mod, test aid: 1 = turn the power on at the start, as the retail switch does");
    _Dvar_RegisterFloat("horde_test_killrate", 0.0f, 0.0f, 1000.0f, 0,
        "horde mod, test aid: kill this many zombies per second as the first player's kills, once the horde has filled");
    _Dvar_RegisterInt("horde_perk_test", 0, 0, 1, 0,
        "horde mod, test aid: 1 = 10 s after the players spawn, drop a nuke at the first player, swap it for a perk bottle and log the perk count");
    _Dvar_RegisterInt("horde_far_test", 0, 0, 1, 0,
        "horde mod, test aid: 1 = a model walks past the +-32768 map box (crash-1249 check)");
    _Dvar_RegisterInt("horde_link_test", 0, 0, 1, 0,
        "horde mod, test aid: 1 = 16 models linked to the highest-numbered zombies (crash-1249 check)");
    _Dvar_RegisterInt("horde_test_fxburst", 0, 0, 1, 0,
        "horde mod, test aid: 1 = once the horde is full every zombie plays the teleporter effect in one frame (entity limit check)");
}
