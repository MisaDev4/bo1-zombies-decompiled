#include maps\_utility;
#include common_scripts\utility;
#include maps\_zombiemode_utility;

// HORDE MOD - not part of the original game. Loaded only with +set fs_game mods/horde.
// Rounds do not wait for the horde to die: the next round starts as soon as the current round's spawn total
// (level.zombie_total) is used up; zombies still alive carry over. Everything else is retail round_think():
// round number, chalk, round music, health/speed/spawn-count scaling, power-ups and the thief round.
// Retail hooks changed: level.round_wait_func (round_start() keeps it; thief_round_tracker() saves and restores it
// around the thief round, which keeps its own retail thief_round_wait()) and level.round_think_func (the retail
// round_think, stopped at end_game: horde_round_think).
// Dvars:
//   horde_max_zombies  live zombie limit (level.zombie_ai_limit, retail 24), read every half second
//   horde_hud          0 (default) hides the live-zombie count, 1 shows it
//   zinfo              1 (default) Tab zombie counter: mods/zinfo, loaded beside horde (fs_mods "horde zinfo"), 0 off
// The engine allows bo1_mod_maxactors actors (default 32, set it on the command line); the limit is clamped to it.

main_start()
{
	// the free-perk bottle (maps\horde\_horde_perks): add_zombie_powerup only registers and precaches what is included
	// before init_powerups. Runs from Five's ffotd hook and from the shared _zombiemode_ffotd one (_hooks.gsc); twice is fine
	maps\_zombiemode_powerups::include_zombie_powerup( "free_perk" );
}


main_end()
{
	if ( GetDvar( #"horde_max_zombies" ) == "" )
	{
		SetDvar( "horde_max_zombies", "24" );
	}
	if ( GetDvar( #"horde_hud" ) == "" )
	{
		SetDvar( "horde_hud", "0" );
	}
	// horde_start_round N (default 0 = retail round 1): start on round N, the way retail's own difficulty and
	// mutator_quickStart settings do it (maps\_zombiemode: first_round false, round_number N before round_start())
	if ( GetDvarInt( #"horde_start_round" ) > 1 )
	{
		level.first_round = false;
		level.round_number = GetDvarInt( #"horde_start_round" );
		// Retail only raises move speed and shortens the spawn delay at the end of each round (round_think), so a
		// late start would still walk like round 1. Apply what rounds 1..N-1 would have done, with retail's formulas.
		level.zombie_move_speed = level.round_number * level.zombie_vars["zombie_move_speed_multiplier"];
		for ( i = 1; i < level.round_number; i++ )
		{
			timer = level.zombie_vars["zombie_spawn_delay"];
			if ( timer > 0.08 )
				level.zombie_vars["zombie_spawn_delay"] = timer * 0.95;
			else if ( timer < 0.08 )
				level.zombie_vars["zombie_spawn_delay"] = 0.08;
		}
	}
	level.round_wait_func = ::horde_round_wait;
	level.round_think_func = ::horde_round_think;
	// mod (L43): horde_fill_rate N = up to N extra zombie spawns per second while the horde is below its cap (0 = retail
	// round_spawning only; default -1 = horde_max_zombies / 60: an empty horde refills in about a minute, 15/s at 900). Retail's loop spawns one zombie per spawn delay + wait_network_frame (0.2 s at round 64+) and
	// a refused DoSpawn (a zombie on the spawner, or the player can see it) still costs the 0.2 s: ~0.6 spawns/s at 900.
	if ( GetDvar( #"horde_fill_rate" ) == "" )
	{
		SetDvar( "horde_fill_rate", "-1" );
	}
	level.round_spawn_func = ::horde_round_spawning;
	level thread horde_limit_watch();
	level thread horde_world_watch();
	level thread horde_hud();
	// Tab zombie counter (dvar zinfo 0/1): mods/zinfo, stacked beside horde (fs_mods), starts itself from its own hook
	level thread horde_round_log();
	level thread horde_tough();
	level thread horde_test_killrate();
	level thread horde_falltrace();
	maps\horde\_horde_windows::init();
	// mod (L52): horde_unjam 0/1 (default 1): a zombie stuck in the crowd loses zombie-vs-zombie collision for 1.5 s
	maps\horde\_horde_unjam::init();
	// mod: horde_perk_drop N (default 15): percent of the regular power-up drops that become a free-perk bottle
	maps\horde\_horde_perks::init();
	// mod (L45): horde_spawn_all 0/1 (default 0 = retail spawn list); manage_zones calls this once a second
	if ( GetDvar( #"horde_spawn_all" ) == "" )
	{
		SetDvar( "horde_spawn_all", "0" );
	}
	level.create_spawner_list_func = ::horde_create_spawner_list;
	level thread horde_open_doors();
	level thread horde_link_test();
	level thread horde_power_on();
	level thread horde_far_test();
	level thread horde_test_fxburst();
	// mod: enemies from other maps, one module each (maps\horde\enemies\), each behind its own dvar. A module needs its
	// map's zone (+set bo1_mod_zones <zone>), so it is reached only from that zone's maps\mod_zones\<zone>.gsc, which the
	// engine loads and runs (after this level main) only when the zone is loaded: horde_enemy_register below.
	level.horde_enemy_modules_started = true;
	if ( IsDefined( level.horde_enemy_modules ) )
	{
		for ( i = 0; i < level.horde_enemy_modules.size; i++ )
			[[ level.horde_enemy_modules[i] ]]();
	}
}


// mod: called by maps\mod_zones\<zone>::main with a module's init; runs it now if the level already reached the call above
horde_enemy_register( initFunc )
{
	if ( !IsDefined( level.horde_enemy_modules ) )
		level.horde_enemy_modules = [];
	level.horde_enemy_modules[level.horde_enemy_modules.size] = initFunc;
	level thread horde_log_later( "enemy module registered (" + level.horde_enemy_modules.size + "), level started " + IsDefined( level.horde_enemy_modules_started ) );
	if ( IsDefined( level.horde_enemy_modules_started ) )
		[[ initFunc ]]();
}


// mod: spawn point for a special enemy module. Temple's _get_special_spawn_point (zombie_temple_spawning.gsc:532) needs
// temple's special_zombie_spawn structs, Five has none: use the origin/angles of a zombie already in the playable area,
// else undefined (the module tries again next second; the old fallback, one of Five's zombie spawners outside the
// barriers, is where napalm #1 of the L33 n3/n4 runs spawned and never got out: farthest 44, died "other" at 30 s).
// mod: a special spawns at least 600 units from every living special. Measured (L33-s10): napalm #1 and sonic #1 took
// the same zombie's spot in the same frame, walked together, and the retail napalm explosion (DoDamage health + 666 on
// every non-napalm zombie within 250 with line of sight, _zombiemode_ai_napalm.gsc) killed the sonic with attacker
// worldspawn. That kill is retail behaviour; sharing the spawn spot was the mod's.
horde_special_spot_clear( zombies, origin )
{
	for ( j = 0; j < zombies.size; j++ )
	{
		o = zombies[j];
		if ( IsAlive( o ) && IsDefined( o.animname ) && o.animname != "zombie" && DistanceSquared( o.origin, origin ) < 600 * 600 )
			return false;
	}
	// a special spawned this frame has no animname yet (L33 shot2: napalm #1 and sonic #1 again shared a spot), so spots
	// handed out in the last 10 s count as specials too
	if ( IsDefined( level.horde_special_picks ) )
	{
		for ( j = 0; j < level.horde_special_picks.size; j++ )
		{
			pick = level.horde_special_picks[j];
			if ( GetTime() - pick.time < 10000 && DistanceSquared( pick.origin, origin ) < 600 * 600 )
				return false;
		}
	}
	return true;
}

horde_special_spawn_point()
{
	zombies = GetAiSpeciesArray( "axis", "all" );
	for ( i = 0; i < zombies.size; i++ )
	{
		z = zombies[i];
		if ( IsDefined( z.animname ) && z.animname == "zombie" && IsDefined( z.completed_emerging_into_playable_area ) && z.completed_emerging_into_playable_area && horde_special_spot_clear( zombies, z.origin ) )
		{
			point = SpawnStruct();
			point.origin = z.origin;
			point.angles = z.angles;
			point.time = GetTime();
			if ( !IsDefined( level.horde_special_picks ) || level.horde_special_picks.size >= 8 )
				level.horde_special_picks = [];
			level.horde_special_picks[level.horde_special_picks.size] = point;
			return point;
		}
	}
	return undefined;
}


// mod: retail round_think (maps\_zombiemode.gsc:3915 loop) has no endon("end_game"): after a solo game over,
// end_game -> destroy_chalk_hud clears level.chalk_hud1/2 and the still-running round_think calls chalk_one_up on them
// (script errors _zombiemode.gsc:3650/3671, also seen in plain-game logs). round_start() uses level.round_think_func
// when set; this runs the retail loop unmodified and only stops it at game over.
horde_round_think()
{
	level endon( "end_game" );
	maps\_zombiemode::round_think();
}


// horde_tough 1 (test aid, default 0): players get 100000 health, refilled every half second, so a
// measurement run can let the horde grow; zombie hits still land and are counted in the log
horde_tough()
{
	flag_wait( "all_players_spawned" );
	players = get_players();
	for ( i = 0; i < players.size; i++ )
	{
		players[i] thread horde_count_hits();
	}
	for ( ;; )
	{
		if ( GetDvarInt( #"horde_tough" ) != 0 )
		{
			players = get_players();
			for ( i = 0; i < players.size; i++ )
			{
				players[i].maxhealth = 100000;
				players[i].health = 100000;
			}
		}
		wait( 0.5 );
	}
}


horde_count_hits()
{
	self endon( "disconnect" );
	self.horde_hits = 0;
	for ( ;; )
	{
		self waittill( "damage", amount, attacker );
		self.horde_hits++;
		if ( self.horde_hits % 25 == 1 )
		{
			horde_log( "player hit " + self.horde_hits + " times (last " + amount + ")" );
		}
	}
}


// mod (L45): horde_spawn_all 1 = zombies spawn from every spawner (window) and riser of every ENABLED zone (its door
// bought, or a start zone) connected to a player's zone, not only the zones the players are in and next to. Retail create_spawner_list
// (maps\_zombiemode_zone_manager) takes zones with is_enabled && is_active; manage_zones rebuilds the list once a second
// through level.create_spawner_list_func after resetting is_active from player_in_zone. Here those zones count
// as active for that one call only, then is_active is put back, so nothing else sees a change. Zones behind closed
// doors stay off. horde_spawn_all 0 calls the retail function unchanged. Read every rebuild: can change while playing.
horde_create_spawner_list( zkeys )
{
	spawn_all = GetDvarInt( #"horde_spawn_all" ) == 1;
	lent = [];
	if ( spawn_all )
	{
		// the enabled zones reachable from the players' zones through retail's own adjacency (the test manage_zones uses
		// for "adjacent": is_connected && is_enabled), followed as far as it goes. Brief said every enabled zone; on Five
		// the floors are enabled but not connected (elevators), and zombies spawned on another floor never reached the
		// player (L45 run: 36 of 64 stood still 800+ units away), so a zone on another floor counts only while a player is there.
		reach = [];
		queue = [];
		for ( z = 0; z < zkeys.size; z++ )
		{
			if ( level.zones[ zkeys[z] ].is_occupied )
			{
				reach[ zkeys[z] ] = true;
				queue[queue.size] = zkeys[z];
			}
		}
		for ( q = 0; q < queue.size; q++ )
		{
			zone = level.zones[ queue[q] ];
			azkeys = GetArrayKeys( zone.adjacent_zones );
			for ( az = 0; az < azkeys.size; az++ )
			{
				if ( zone.adjacent_zones[ azkeys[az] ].is_connected && level.zones[ azkeys[az] ].is_enabled && !IsDefined( reach[ azkeys[az] ] ) )
				{
					reach[ azkeys[az] ] = true;
					queue[queue.size] = azkeys[az];
				}
			}
		}
		for ( q = 0; q < queue.size; q++ )
		{
			zone = level.zones[ queue[q] ];
			if ( zone.is_enabled && !zone.is_active )
			{
				zone.is_active = true;
				lent[lent.size] = zone;
			}
		}
	}
	maps\_zombiemode_zone_manager::create_spawner_list( zkeys );
	horde_spawn_list_log( zkeys, spawn_all );
	for ( i = 0; i < lent.size; i++ )
	{
		lent[i].is_active = false;
	}
}


// logs "horde: spawn list ..." when the spawning zones change; with bo1_measure 1 also each zone's spawner origins
// once ("horde_zone: ...", for matching actor_spawn origins to zones)
horde_spawn_list_log( zkeys, spawn_all )
{
	if ( !IsDefined( level.horde_zone_dumped ) && GetDvarInt( #"bo1_measure" ) == 1 )
	{
		level.horde_zone_dumped = true;
		for ( z = 0; z < zkeys.size; z++ )
		{
			zone = level.zones[ zkeys[z] ];
			s = "";
			for ( x = 0; x < zone.spawners.size; x++ )
				s += " " + Int( zone.spawners[x].origin[0] ) + "," + Int( zone.spawners[x].origin[1] ) + "," + Int( zone.spawners[x].origin[2] );
			for ( x = 0; x < zone.rise_locations.size; x++ )
				s += " " + Int( zone.rise_locations[x].origin[0] ) + "," + Int( zone.rise_locations[x].origin[1] ) + "," + Int( zone.rise_locations[x].origin[2] );
			LogPrint( "horde_zone: " + zkeys[z] + s + "\n" );
		}
	}
	zones = "";
	here = "";
	for ( z = 0; z < zkeys.size; z++ )
	{
		zone = level.zones[ zkeys[z] ];
		if ( zone.is_enabled && zone.is_active )
			zones += " " + zkeys[z];
		if ( zone.is_occupied )
			here += " " + zkeys[z];
	}
	if ( IsDefined( level.horde_spawn_list_last ) && level.horde_spawn_list_last == zones )
		return;
	level.horde_spawn_list_last = zones;
	horde_log( "spawn list " + level.enemy_spawns.size + " spawners " + level.zombie_rise_spawners.size + " risers, spawn_all " + spawn_all + ", zones:" + zones + " | players in:" + here );
}


// mod (L45): horde_open_doors 1 - a test aid (like horde_tough): at the start, opens every door, airlock and debris the
// way the doors' own buy triggers do (retail zombie_devgui_open_sesame; door_buy / debris_think skip the cost while
// zombie_unlock_all is set), so every zone gets enabled. As mods/sandbox's sandbox_open_sesame.
horde_open_doors()
{
	flag_wait( "begin_spawning" );
	if ( GetDvarInt( #"horde_open_doors" ) != 1 )
		return;
	player = get_players()[0];
	SetDvar( "zombie_unlock_all", 1 );
	names = array( "zombie_door", "zombie_airlock_buy", "zombie_debris" );
	n = 0;
	for ( k = 0; k < names.size; k++ )
	{
		ents = GetEntArray( names[k], "targetname" );
		for ( i = 0; i < ents.size; i++ )
		{
			ents[i] notify( "trigger", player );
			n++;
			wait( .05 );
		}
	}
	wait( 1 );
	SetDvar( "zombie_unlock_all", 0 );
	horde_log( "open doors: " + n + " triggers" );
}


// mod (L51): horde_power_on 1 - a test aid: turns the power on at the start the way the retail switch does (the map's
// electric switch thread waits on the use_elec_switch trigger, maps_zombie_theater.gsc 348; as mods/sandbox's
// sandbox_power_on). Kino's quads (and their fx_quad_trail links) only come after power (quad_wave_init).
horde_power_on()
{
	flag_wait( "begin_spawning" );
	if ( GetDvarInt( #"horde_power_on" ) != 1 )
		return;
	if ( flag( "power_on" ) )
		return;
	trig = GetEnt( "use_elec_switch", "targetname" );
	if ( IsDefined( trig ) )
		trig notify( "trigger", get_players()[0] );
	flag_wait( "power_on" );
	horde_log( "power on" );
}


// mod (L51): horde_far_test 1 - a test aid: a script_model that walks slowly (20 units per server frame, the snapshot's
// small-delta path) from Y 32600 to Y 33000, past the MP +-32768 box around the map center (Kino: center y 37, box to
// 32805), as the entity of crash-1249 (Y 32810). The MP exe Com_Errors there; the SP exe sends x/y in 18 bits.
// tools/mergecheck.sh check_farlink expects its BO1_FARENT line and no "too far outside" error.
horde_far_test()
{
	if ( GetDvarInt( #"horde_far_test" ) != 1 )
		return;
	flag_wait( "begin_spawning" );
	wait( 5 );
	org = get_players()[0].origin;
	m = Spawn( "script_model", ( org[0], 32600, org[2] ) );
	m SetModel( "tag_origin" );
	m.targetname = "horde_far_test";
	horde_log( "far test: ent " + m GetEntityNumber() );
	for ( y = 32600; y <= 33000; y += 20 )
	{
		m.origin = ( org[0], y, org[2] );
		wait( .05 );
	}
	horde_log( "far test: at y " + m.origin[1] );
}


// mod (L65): horde_test_fxburst 1 - a test aid for "G_Spawn: no free entities" at a full horde. Once the horde is full
// (or after 120 s), every zombie plays the tail of Five's maps\zombie_pentagon_teleporter::send_zombies_out in the same
// frame: PlayFX( transporter_start, origin ) + playsoundatposition( "evt_teleporter_out", origin ). Retail reaches that
// frame when check_if_empty_floors sends a floor's zombies out (elevator / teleporter ride, or 2 s after a player's
// "spawned_spectator") and their 20 s teleportation_timed_out timers, started together, expire together.
// tools/mergecheck.sh check_tempent: before L65 the map ended in G_Spawn, with the engine's temp-entity guards it runs on.
horde_test_fxburst()
{
	if ( GetDvarInt( #"horde_test_fxburst" ) != 1 )
		return;
	flag_wait( "begin_spawning" );
	start = GetTime();
	while ( horde_alive() < level.zombie_ai_limit && GetTime() - start < 120000 )
		wait( 0.5 );
	fx = level._effect["transporter_start"];
	if ( !IsDefined( fx ) )
	{
		keys = GetArrayKeys( level._effect );
		fx = level._effect[keys[0]];
	}
	zombies = GetAiSpeciesArray( "axis", "all" );
	horde_log( "fxburst: " + zombies.size + " zombies at time " + GetTime() );
	for ( i = 0; i < zombies.size; i++ )
	{
		if ( !IsDefined( zombies[i] ) || !IsAlive( zombies[i] ) )
			continue;
		PlayFX( fx, zombies[i].origin );
		playsoundatposition( "evt_teleporter_out", zombies[i].origin );
	}
	horde_log( "fxburst: done at time " + GetTime() );
}

// mod (L51): horde_link_test 1 - a test aid: 16 script_models, spawned before the horde fills the low entity numbers
// (as Kino's quad fx_quad_trail 877 / 1014 in crash-1249), kept linked to the 16 highest-numbered zombies (1537+,
// bo1_mod_netents) exactly as a quad carries its trail (_zombiemode_ai_quad.gsc 798-801). With the spawn_all horde the
// snapshot is past its 1024 cap, which cuts the top of the entity list first (crash-1249: parents 1754..1776):
// tools/mergecheck.sh check_farlink counts the client's "parent ent #N not in snapshot" warnings and BO1_LINKHI.
horde_link_test()
{
	if ( GetDvarInt( #"horde_link_test" ) != 1 )
		return;
	flag_wait( "begin_spawning" );
	trails = [];
	for ( i = 0; i < 16; i++ )
	{
		trails[i] = Spawn( "script_model", ( 0, 0, 0 ) );
		trails[i] SetModel( "tag_origin" );
	}
	horde_log( "link test: trails " + trails[0] GetEntityNumber() + ".." + trails[15] GetEntityNumber() );
	while ( 1 )
	{
		zombies = GetAiSpeciesArray( "axis", "all" );
		used = [];
		for ( k = 0; k < trails.size; k++ )
		{
			z = undefined;
			for ( i = 0; i < zombies.size; i++ )
			{
				n = zombies[i] GetEntityNumber();
				if ( IsAlive( zombies[i] ) && n >= 1537 && !IsDefined( used[n] ) && ( !IsDefined( z ) || n > z GetEntityNumber() ) )
					z = zombies[i];
			}
			if ( !IsDefined( z ) )
				break;
			used[z GetEntityNumber()] = true;
			if ( IsDefined( trails[k].horde_link_parent ) && trails[k].horde_link_parent == z )
				continue;
			trails[k] Unlink();
			trails[k].origin = z GetTagOrigin( "tag_origin" );
			trails[k].angles = z GetTagAngles( "tag_origin" );
			trails[k] LinkTo( z, "tag_origin" );
			trails[k].horde_link_parent = z;
		}
		wait( .5 );
	}
}

horde_log_later( msg )
{
	wait 1;
	horde_log( "(logged 1 s late) " + msg );
}


horde_log( msg )
{
	LogPrint( "horde: " + msg + "\n" );
}


// retail round_wait() (maps\_zombiemode), with the "all enemies dead" condition dropped for normal rounds
horde_round_wait()
{
	wait( 1 );

	if ( flag( "dog_round" ) )
	{
		wait( 7 );
		while ( level.dog_intermission )
		{
			wait( 0.5 );
		}
		return;
	}

	while ( level.zombie_total > 0 || level.intermission )
	{
		if ( flag( "end_round_wait" ) )
		{
			return;
		}
		wait( 0.5 );
	}
	// horde_carryover 0 (user): the round also waits until every zombie is dead, as retail round_wait() does; the horde's
	// size (horde_max_zombies) still applies. 1 (default): the next round starts now, alive zombies carry over.
	if ( GetDvar( #"horde_carryover" ) == "0" )
	{
		while ( get_enemy_count() > 0 || level.zombie_total > 0 || level.intermission )
		{
			if ( flag( "end_round_wait" ) )
			{
				return;
			}
			wait( 0.5 );
		}
		horde_log( "round " + level.round_number + " cleared" );
		return;
	}
	horde_log( "round " + level.round_number + " spawn total used up, alive " + horde_alive() + " carry over" );
}


// mod (L43): level.round_spawn_func. Retail round_spawning unchanged (it sets the round's zombie_total and keeps its own
// one-at-a-time loop); horde_fill runs beside it. The dog / thief / crawler round trackers save and restore this pointer.
horde_round_spawning()
{
	level thread horde_fill();
	maps\_zombiemode::round_spawning();
}


// Spends zombie_total as retail does (spawn_zombie, zombie_total--, round_spawn_failsafe), at up to horde_fill_rate
// spawns per second (a budget of at most one second's spawns), up to 8 attempts per frame from different spawners.
// A refused spawn gives its budget back, so the next frame retries elsewhere.
horde_fill()
{
	level endon( "intermission" );
	level endon( "end_of_round" );
	level endon( "restart_round" );
	if ( !IsDefined( level.horde_fill_pending ) )
	{
		level.horde_fill_pending = 0;
	}
	level.horde_fill_budget = 0;
	for ( ;; )
	{
		wait( 0.05 );
		rate = GetDvarFloat( #"horde_fill_rate" );
		if ( rate < 0 )
		{
			rate = level.zombie_ai_limit / 60.0;
		}
		if ( rate <= 0 || !flag( "spawn_zombies" ) || level.enemy_spawns.size < 1 )
		{
			level.horde_fill_budget = 0;
			continue;
		}
		level.horde_fill_budget += rate * 0.05;
		if ( level.horde_fill_budget > rate )
		{
			level.horde_fill_budget = rate;
		}
		tries = int( level.horde_fill_budget );
		room = level.zombie_ai_limit - horde_alive() - level.horde_fill_pending;
		left = level.zombie_total - level.horde_fill_pending;
		if ( tries > 8 )
			tries = 8;
		if ( tries > room )
			tries = room;
		if ( tries > left )
			tries = left;
		if ( tries > level.enemy_spawns.size )
			tries = level.enemy_spawns.size;
		// distinct spawners this frame (the engine refuses a second spawn from one spawner in a frame)
		start = RandomInt( level.enemy_spawns.size );
		for ( i = 0; i < tries; i++ )
		{
			level.horde_fill_budget -= 1;
			level.horde_fill_pending++;
			level thread horde_fill_one( level.enemy_spawns[( start + i ) % level.enemy_spawns.size] );
		}
	}
}


horde_fill_one( spawn_point )
{
	ai = maps\_zombiemode_utility::spawn_zombie( spawn_point );
	level.horde_fill_pending--;
	if ( IsDefined( ai ) )
	{
		level.zombie_total--;
		ai thread maps\_zombiemode::round_spawn_failsafe();
	}
	else
	{
		level.horde_fill_budget += 1;
	}
}


// "fill alive N time T" the first time the horde reaches each hundred (measurement: time to 600 / 900)
horde_fill_log( alive )
{
	if ( !IsDefined( level.horde_fill_mark ) )
	{
		level.horde_fill_mark = 0;
	}
	if ( alive >= level.horde_fill_mark + 100 )
	{
		level.horde_fill_mark = int( alive / 100 ) * 100;
		horde_log( "fill alive " + level.horde_fill_mark + " time " + GetTime() + " round " + level.round_number );
	}
	// the horde rarely shows exactly its cap (dying zombies still hold an actor slot): "fill full" at 99%
	if ( !IsDefined( level.horde_fill_full ) && alive >= level.zombie_ai_limit * 0.99 )
	{
		level.horde_fill_full = true;
		horde_log( "fill full alive " + alive + " time " + GetTime() + " round " + level.round_number );
	}
}


horde_alive()
{
	ai = GetAiSpeciesArray( "axis", "all" );
	return ai.size;
}


horde_engine_max()
{
	engine_max = GetDvarInt( #"bo1_mod_maxactors" );
	if ( engine_max < 32 )
	{
		engine_max = 32;
	}
	return engine_max;
}


// level.zombie_ai_limit is read by retail round_spawning() before every spawn; SetAILimit() is the engine cap
horde_limit_watch()
{
	last = -1;
	for ( ;; )
	{
		limit = GetDvarInt( #"horde_max_zombies" );
		if ( limit < 1 )
		{
			limit = 1;
		}
		if ( limit > horde_engine_max() )
		{
			limit = horde_engine_max();
		}
		if ( limit != last )
		{
			level.zombie_ai_limit = limit;
			SetAILimit( limit );
			// above retail's 24, the retail 30 s failsafe (round_spawn_failsafe) kills every zombie that is packed
			// around a player and cannot move, and re-queues it: that capped the horde at ~85. Off above 24.
			level.zombie_vars["zombie_use_failsafe"] = ( limit <= 24 );
			horde_log( "zombie limit " + limit + " (engine actors " + horde_engine_max() + ")" );
			last = limit;
		}
		horde_fill_log( horde_alive() );
		wait( 0.5 );
	}
}


// mod (L50d): disabling the retail stationary-crowd timeout must not disable its below-world check.
// Retail maps\_zombiemode::round_spawn_failsafe uses below_world_check, optionally requeues, then DoDamage.
// Poll this independent branch every 0.5 s: its shared 30 s wait lets a falling horde actor reach FARENT first.
horde_world_watch()
{
	for ( ;; )
	{
		wait( 0.5 );
		if ( level.zombie_ai_limit <= 24 || !IsDefined( level.zombie_vars["below_world_check"] ) )
			continue;
		ai = GetAiSpeciesArray( "axis", "all" );
		for ( i = 0; i < ai.size; i++ )
		{
			z = ai[i];
			if ( !IsAlive( z ) || is_true( z.ignore_round_spawn_failsafe ) || z.origin[2] >= level.zombie_vars["below_world_check"] )
				continue;
			if ( !is_true( z.horde_world_requeued ) )
			{
				z.horde_world_requeued = true;
				if ( is_true( level.put_timed_out_zombies_back_in_queue ) && !flag( "dog_round" ) )
					level.zombie_total++;
				horde_log( "below_world time " + GetTime() + " ent " + z GetEntityNumber() + " origin " + z.origin );
			}
			z DoDamage( z.health + 100, ( 0, 0, 0 ) );
		}
	}
}

// mod (L50d): script half of bo1_falltrace, once per actor after a 128-unit drop.
horde_falltrace()
{
	if ( !GetDvarInt( #"bo1_falltrace" ) )
		return;
	for ( ;; )
	{
		wait( 0.25 );
		ai = GetAiSpeciesArray( "axis", "all" );
		for ( i = 0; i < ai.size; i++ )
		{
			z = ai[i];
			if ( !IsDefined( z.horde_fall_top ) || z.origin[2] > z.horde_fall_top )
				z.horde_fall_top = z.origin[2];
			if ( is_true( z.horde_fall_logged ) || z.origin[2] >= z.horde_fall_top - 128 )
				continue;
			z.horde_fall_logged = true;
			script = "-";
			if ( IsDefined( z.a ) && IsDefined( z.a.script ) )
				script = z.a.script;
			node = ( 0, 0, 0 );
			if ( IsDefined( z.first_node ) )
				node = z.first_node.origin;
			horde_log( "fallstate time " + GetTime() + " ent " + z GetEntityNumber() + " origin " + z.origin + " health " + z.health + " script " + script + " traverse " + is_true( z.is_traversing ) + " emerged " + is_true( z.completed_emerging_into_playable_area ) + " in_ground " + is_true( z.in_the_ground ) + " gibbed " + is_true( z.gibbed ) + " nocol " + z.noactorcollision + " window " + is_true( z.horde_win_nocol ) + " unjam " + IsDefined( z.horde_uj_ghost_end ) + " firstnode " + node );
		}
	}
}

// TEST SWITCH (L50): horde_test_killrate N (default 0 = off) kills N zombies per second as player kills: DoDamage of
// health + 666 with the first player as attacker ("riflebullet" to the head, so the retail death path runs:
// points, power-up drops, death anims, gibs, corpses), and the horde refills. One "killrate" line per 5 s for churn counts.
horde_test_killrate()
{
	if ( GetDvar( #"horde_test_killrate" ) == "" )
	{
		SetDvar( "horde_test_killrate", "0" );
	}
	// Fill once before starting churn, so the test measures replacement at the requested capacity.
	filled = false;
	kills = 0;
	survived = 0;
	budget = 0;
	next_log = GetTime() + 5000;
	for ( ;; )
	{
		wait( 0.05 );
		rate = GetDvarFloat( #"horde_test_killrate" );
		if ( rate <= 0 )
		{
			budget = 0;
			continue;
		}
		players = get_players();
		if ( !filled && horde_alive() >= level.zombie_ai_limit )
		{
			filled = true;
			horde_log( "killrate filled alive " + horde_alive() + " time " + GetTime() );
		}
		// This is a capacity/churn test: pause during the retail between-round wait, when no replacements can spawn.
		// Otherwise 20 deaths/s drains another ~240 actors in the 12 s intermission alone.
		if ( filled && players.size > 0 && IsAlive( players[0] ) && flag( "spawn_zombies" ) && level.zombie_total > 0 )
		{
			budget += rate * 0.05;
			// Fractional rates must be able to accumulate one whole hit.
			cap = rate;
			if ( cap < 1 )
				cap = 1;
			if ( budget > cap )
				budget = cap;
			ai = GetAiSpeciesArray( "axis", "all" );
			start = 0;
			if ( ai.size > 0 )
				start = RandomInt( ai.size );
			for ( i = 0; i < ai.size && budget >= 1; i++ )
			{
				zombie = ai[( start + i ) % ai.size];
				if ( !IsDefined( zombie ) || !IsAlive( zombie ) || zombie.health <= 0 )
					continue;
				// a zombie still rising from its spawn (retail sets completed_emerging_into_playable_area at the end of its
				// rise, maps\_zombiemode_spawner) is left for later, as a player's gun rarely meets one
				if ( !is_true( zombie.completed_emerging_into_playable_area ) )
					continue;
				budget -= 1;
				before = zombie.health;
				// mod (L50d): correlate a forced hit with native fall history; diagnostic only.
				if ( GetDvarInt( #"bo1_falltrace" ) )
				{
					script = "-";
					if ( IsDefined( zombie.a ) && IsDefined( zombie.a.script ) )
						script = zombie.a.script;
					horde_log( "fallhit time " + GetTime() + " ent " + zombie GetEntityNumber() + " origin " + zombie.origin + " script " + script + " nocol " + zombie.noactorcollision + " emerged " + is_true( zombie.completed_emerging_into_playable_area ) + " in_ground " + is_true( zombie.in_the_ground ) );
				}
				// Limb/torso scaling can leave round-100 zombies alive; use head damage for a lethal hit.
				zombie DoDamage( zombie.health + 666, players[0].origin + ( 0, 0, 60 ), players[0], undefined, "riflebullet", "head" );
				if ( IsDefined( zombie ) && IsAlive( zombie ) )
				{
					survived++;
					// the first few survivors, for evidence (why a kill did not take)
					if ( survived <= 5 )
						horde_log( "killrate survived health " + before + " -> " + zombie.health + " takedamage " + zombie.takedamage + " emerged " + is_true( zombie.completed_emerging_into_playable_area ) + " in_ground " + is_true( zombie.in_the_ground ) + " time " + GetTime() );
				}
				else
					kills++;
			}
		}
		else
			budget = 0;
		if ( GetTime() >= next_log )
		{
			next_log = GetTime() + 5000;
			all = 0;
			if ( IsDefined( level.total_zombies_killed ) )
				all = level.total_zombies_killed;
			horde_log( "killrate kills " + kills + " alive " + horde_alive() + " left " + level.zombie_total + " time " + GetTime() + " round " + level.round_number + " all " + all + " survived " + survived );
		}
	}
}


horde_round_log()
{
	for ( ;; )
	{
		level waittill( "start_of_round" );
		horde_log( "start round " + level.round_number + ", spawn total " + level.zombie_total + ", health " + level.zombie_health + ", alive " + horde_alive() );
	}
}


horde_hud_elem( x, y )
{
	hud = NewHudElem();
	hud.alignX = "left";
	hud.alignY = "top";
	hud.horzAlign = "left";
	hud.vertAlign = "top";
	hud.x = x;
	hud.y = y;
	hud.fontScale = 1.2;
	hud.foreground = true;
	hud.hidewheninmenu = true;
	hud.alpha = 1;
	return hud;
}


// small top-left text: "horde alive <n> / limit <n>" (numbers by SetValue, no new strings per update)
horde_hud()
{
	flag_wait( "all_players_connected" );

	label = horde_hud_elem( 4, 90 );
	label SetText( "horde alive" );
	alive = horde_hud_elem( 64, 90 );
	label2 = horde_hud_elem( 94, 90 );
	label2 SetText( "/ limit" );
	limit = horde_hud_elem( 132, 90 );

	last_log = 0;
	for ( ;; )
	{
		a = 1;
		if ( GetDvarInt( #"horde_hud" ) == 0 )
		{
			a = 0;
		}
		label.alpha = a;
		alive.alpha = a;
		label2.alpha = a;
		limit.alpha = a;
		n = horde_alive();
		alive SetValue( n );
		limit SetValue( level.zombie_ai_limit );
		if ( GetTime() - last_log >= 5000 )
		{
			horde_log( "alive " + n + " limit " + level.zombie_ai_limit + " round " + level.round_number + " total " + level.zombie_total );
			last_log = GetTime();
		}
		wait( 0.25 );
	}
}
