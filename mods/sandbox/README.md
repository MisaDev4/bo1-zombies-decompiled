# sandbox - a test area on Five (NOT part of the original game)

A mod folder, kept apart from the base game. It turns Five (zombie_pentagon) into a sandbox: power on, every
door and debris open, DEFCON 5 (the portals lead to Pack-a-Punch), points, and a field of use-pads in the
conference room (the spawn room) that give every perk, power-up and weapon and control the rounds.

The base game never reads this folder. It is loaded only when you start the game with `+set fs_game mods/sandbox`;
without that, Five is the unchanged original.

## Start it

The build copies `mods\` next to the exe (`build\Release\mods\sandbox\`). Start Five the way you normally do,
plus the one setting `+set fs_game mods/sandbox`. In PowerShell, from the build folder:

```powershell
cd build\Release
$R = (Get-Location).Path
$G = (Get-Content ..\..\gamedir.txt -TotalCount 1).Trim()   # your Black Ops install, recorded by setup.ps1
.\BO1Zombies.exe +set fs_b $R +set fs_h $R +set fs_cdpath $G +set r_fullscreen 0 +set bo1_zombies 1 +set fs_game mods/sandbox +devmap zombie_pentagon
```

(`+set r_fullscreen 1` instead for full screen.) These are the settings the headless checks start the game with
(`tools\headless.ps1 -Zombies`), minus the headless-only ones.

## Leave it

Quit, and start the game again without `+set fs_game mods/sandbox`: that is the normal, unchanged Five. The
setting is not saved in any config file (fs_game is not an archived dvar), so it never sticks. Deleting
`build\Release\mods\` removes the mod completely (the next build copies it back).

While the mod is loaded the game writes its logs to `build\Release\mods\sandbox\` (console_mp.log, games_mp.log;
every pad logs a `sandbox: ...` line in games_mp.log).

## Graphics settings in the sandbox

As in the original PC game, a mod keeps its own settings file: with `fs_game mods/sandbox` the game reads and writes
`players\mods\sandbox\config.cfg` (under the folder the game runs from, `fs_homepath`), not `players\config.cfg`. The
first time the sandbox runs, that file does not exist, so the sandbox starts with the game's defaults, not your
settings: vsync on at 60 Hz (`r_vsync 1`, `r_displayRefresh "60 Hz"`), `com_maxfps 85`, `r_depthPrepass 1`, ... With
vsync on, every frame that takes longer than 16.7 ms is shown for two refreshes (30 fps for that moment). the user's
playtest of 2026-09-24 15:30 ran like that (the recorder shows 60 fps throughout; `players\mods\sandbox\config.cfg`
has `r_vsync "1"`, `com_maxfps "85"`). To use your normal settings in the sandbox, copy `players\config.cfg` to
`players\mods\sandbox\config.cfg` once (with the game closed), or change them in the sandbox's options menu.

## The pads

Walk onto a pad (a floating model on the conference room floor) and press Use. Each pad shows its hint text.

| pad | what it does |
| --- | --- |
| DEFCON 5 | uses the four DEFCON switches until DEFCON 5 (already done at start; the map's own countdown resets it later, as in the game) |
| Pack-a-Punch | upgrades the weapon in your hands (as the devgui does) |
| points | +100000 points |
| ammo | full ammo and full clips for every weapon you carry |
| god | god mode on / off |
| spawning | zombie spawning on / off |
| next round | ends the current round; the next one starts after the usual break |
| set round | goes to round `sandbox_round` (default 20; `/set sandbox_round 8` in the console to change it) |
| thief | makes the next round a thief round |
| crawler | spawns one crawler |
| doors | opens every door and debris again |
| perk (4 pads) | Double Tap, Speed Cola, Quick Revive, Juggernog: each pad uses that perk's machine (its price, its bottle, its perk) |
| power-up (8 pads) | drops that power-up in front of you: Death Machine, Bonfire Sale, Fire Sale, Carpenter, Max Ammo, Double Points, Insta-Kill, Nuke |
| weapon (35 pads) | gives that wall or box weapon (the Mystery Box's own give function), including the Ray Gun, Winter's Howl, Ballistic Knife, Crossbow, Monkey Bombs, Claymores |

The next-round, set-round and thief pads wait for the current round to have started if you use them during the
break between rounds.

## Settings (console dvars, set before `devmap`)

- `sandbox_points` - points at the start (default 500000).
- `sandbox_round` - the round the set-round pad goes to (default 20).
- `sandbox_selftest 1` - for the headless checks only: uses every pad once and logs the results (3: the same, first
  game only; 2: camera views over the pad field for screenshots; `sandbox_selftest_skip <text>` skips the pads whose
  label contains the text).

## How it works

- `maps/zombie_pentagon_ffotd.gsc` replaces the map's own "fix for the day" hook (in the original game both of its
  functions are empty). Its only content is one call each into the sandbox script.
- `maps/sandbox/_sandbox.gsc` is the sandbox. Every effect calls the original scripts' own functions (the power
  switch, the door triggers, the DEFCON switches, the perk machines, `specific_powerup_drop`, `weapon_give`, ...).
- The game looks for a script in the `fs_game` folder before the fastfiles (as the original PC game does for mods);
  with no `fs_game` set that folder is not on the search path at all.
