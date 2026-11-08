# horde - rounds that don't wait for the horde (NOT part of the original game)

A mod folder, kept apart from the base game, like `mods/sandbox`. On Five (zombie_pentagon):

- The next round starts as soon as the current round's last zombie has spawned (its spawn total is used up).
  Zombies still alive carry over. Everything else is the retail round logic: round number, chalk, round music,
  zombie health / speed / spawn count per round, the between-round pause, power-ups, the thief round.
- `horde_max_zombies` sets how many zombies may be alive at once (retail 24). You can change it while playing
  (console: `/horde_max_zombies 40`); it applies within half a second.
- `horde_hud 1` shows a small counter at the top left: `horde alive <n> / limit <n>` (off by default). `bo1_fpscounter 1` shows FPS at the top left.

The base game never reads this folder. Without `+set fs_mods horde zinfo`, Five is the unchanged original. Horde
stacks with other mods (`fs_mods`, mods/_stack/maps/_modstack.gsc): the Tab counter is mods/zinfo, `noperks` is
mods/noperks, a mapkit map goes first (`fs_mods mapkit horde zinfo`). fs_game mods/horde only picks the log folder.

## Start it

The build copies `mods\` next to the exe (`build\Release\mods\horde\`). In PowerShell, from the build folder:

```powershell
cd build\Release
$R = (Get-Location).Path
$G = (Get-Content ..\..\gamedir.txt -TotalCount 1).Trim()   # your Black Ops install, recorded by setup.ps1
.\BO1Zombies.exe +set fs_b $R +set fs_h $R +set fs_cdpath $G +set r_fullscreen 0 +set bo1_zombies 1 +set fs_game mods/horde +set fs_mods horde zinfo +set bo1_mod_maxactors 64 +set horde_max_zombies 48 +devmap zombie_pentagon
```

## More than 32 zombies

The engine holds 32 AI (retail). `bo1_mod_maxactors` (default 32, range 32-1024) raises that. It must be on the
command line (it is read when the map loads; changing it later needs a map restart). `horde_max_zombies` is clamped
to it. Leave a few above the zombie limit for the thief: retail keeps 24 of 32.

Above 24 the mod turns off retail's 30 s "stuck zombie" failsafe (it killed and re-queued the crowd around a player).
On Five the game's 1022 entities are the real ceiling (the map keeps ~790 in use without zombies): about 150-160
zombies alive. The engine then simply refuses further zombie spawns (a console line "mod: actor spawn refused at
the entity limit") instead of ending the map, so a higher `horde_max_zombies` is safe, it just is not reached.

## Other dvars

- `horde_perk_drop N` (default 15) - percent of the zombie power-up drops that are a free-perk bottle, the one Call of the
  Dead's George drops (retail never lets a zombie kill pick it). 100 = every zombie drop, 0 = retail. Grabbing it gives every
  living player one perk they don't have yet, from the machines of the map. Between 1 and 99 the share is approximate (the
  bottle gets as many slots in retail's drop list as that share of the plain names needs, and retail skips carpenter / fire
  sale / revive all / death machine at times). Drops a script makes with a name (the dog round's max ammo, the thief, George,
  a quantum bomb) and the monkey round's are not touched. A drop is only a bottle while somebody still lacks a perk; ignored
  with `noperks`. Can be changed while playing (type `/horde_perk_drop 100`: without the slash the console sends chat).
  Every drop is logged as `horde_perk: drop <name>` in games_mp.log (`free_perk` = a bottle).
  (`maps/horde/_horde_perks.gsc`: `main_start` in `_horde.gsc` includes the power-up before the retail power-up init so its
  model is loaded. `horde_perk_test 1` is a test aid that drops one and logs the perk count before and after.)
- `horde_drop_max N`, `horde_drop_step N`, `horde_drop_growth X` (default -1 = retail) - how often zombies drop power-ups.
  Retail drops one whenever the team's total score has grown by a gap: the first gap is 2000 (on top of the start score),
  then each gap is 1.14 times the last (2000, 2280, 2599, ...), and at most 4 drops per round (values from Der Riese's zombie
  vars table). `horde_drop_step` sets the first gap, `horde_drop_growth` what it is multiplied by after each drop (1 = always
  the same gap), `horde_drop_max` the drops per round (0 = none). Read while playing. With all three at -1 nothing is touched.
  Logged as `horde_drop: drop N handed out, next gap G`. (`maps/horde/_horde_drops.gsc`; example for a drop every ~300 points,
  up to 20 a round: `+set horde_drop_step 300 +set horde_drop_growth 1 +set horde_drop_max 20`.)
- `horde_tough 1` - a test aid: players get 100000 health, refilled every half second, so the horde can grow.
- `zinfo 1` (default) - while you hold Tab, the zombies alive and the zombies left this round show beside the
  scoreboard (`zinfo 0` hides them). The same counter without horde rules: `mods/zinfo`.
- `horde_start_round N` - start on round N (command line, e.g. `+set horde_start_round 100`): round number, zombie
  speed and spawn delay as if rounds 1..N-1 had been played. Default 0 = round 1.
- `horde_spawn_all 1` - zombies spawn from every window of every opened area (a bought door's zone, or a start zone)
  that connects to where a player is, not only from the zones you are in and next to (retail, `0`, the default), so
  the whole open map fills. Areas behind closed doors stay off, and so do Five's other floors while nobody is on them
  (only the elevators link them; zombies spawned there never reached the player). Can be changed while playing. Launcher: "Spawn from every window".
- `horde_open_doors 1` - a test aid: opens every door, airlock and debris at the start (free).
- `horde_test_killrate N` - a test aid: kills N zombies per second as the first player's kills (points, power-ups,
  death anims, gibs), starting after the horde first fills to its cap, then refilling even at round 100.
  Uses lethal head hits; pauses between rounds while replacements cannot spawn, and logs `horde: killrate kills ...`
  every 5 s. Default 0. For a 600-zombie churn test at 20 kills/s, use `horde_fill_rate 30` (the default is 10/s).

The mod logs `horde: ...` lines (round starts, alive count every 5 s, limit changes) to
`build\Release\mods\horde\games_mp.log`.

`bo1_mod_pcore` (engine, default 0; the launcher's horde flags set 1): on a hybrid Intel CPU (P- and E-cores) the
listen-server thread is kept on the P-cores at above-normal priority; 2 also keeps the client threads there. Windows 10
otherwise often runs it on an E-core (Kino 1000: server frame p50 96 -> 78 ms). Does nothing on other CPUs.

## Windows (big hordes)

Retail lets 3 zombies at a time work a barrier window (its 3 "attack spots"); the rest wait in a crowd. Even with every
board down, a zombie must reach its spot (within 2 units) before it climbs through, and with 100+ zombies the crowd
keeps it from getting there: in two 10-minute runs at 150 alive, 7 and 38 zombies stayed outside for the whole run.
`maps/horde/_horde_windows.gsc` fixes this above the retail limit of 24 (at 24 or below it does nothing):

- `horde_window_fix` (default 1) is a set of bits. 0 = off (retail).
  - 1: from the time a zombie reaches its window until it has climbed through, other zombies don't block it and it
    doesn't step around them, so it walks through the crowd to its spot. After that it steers around others as normal.
    Bullets still hit it. This uses the engine's `noactorcollision` actor field (a BO1Zombies addition; retail scripts
    never set it).
  - 2: script helpers. With the boards down, free the spots and let the zombies climb through. A zombie that has waited
    4 s at a crowded window goes to the least crowded of its own nearby windows. A zombie that hasn't moved in 20 s and
    is 800+ units from every player is put back in the spawn queue.
  - 3: both.
- `horde_window_log` (default 1) logs `horde_win: ...` every 5 s: zombies still approaching, waiting at a window,
  inside, "stuckfar" (hasn't moved 48 units in 10 s and is 400+ units from every player), near a player, and
  `nocol` (collision off). 2 also lists where the stuck zombies are.

`maps/horde/_horde_unjam.gsc` (L52): in a big crowd zombies stop each other anywhere on the map (each waits for its
neighbour to move away). A zombie that has not moved 16 units in 1 s (out of the window phase, 96+ units from every player,
not in melee or a traverse) gets `noactorcollision` for 1.5 s, again while still stuck, 3 times in a row then a 5 s rest;
collision comes back at once within 96 units of a player. The window fix and this share the field (reason bits, neither
turns the other's off).
- `horde_unjam` (default 1). 0 = off (retail steering). Launcher: "Unstick zombies".
- `horde_unjam_log` (default 1) logs `horde_uj: ...` every 5 s, one line per stuck spell and per zombie reaching a player;
  2 also lists where stuck zombies are. `node tools/unjam_table.mjs tag=games_mp.log,console_mp.log` makes the table.
