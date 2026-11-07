#include maps\_utility;
#include common_scripts\utility;
#include maps\_zombiemode_utility;

// HORDE MOD - not part of the original game. Free-perk bottles as ordinary zombie power-up drops. Retail only drops
// "free_perk" from Call of the Dead's George Romero (and the teleporter special drop): maps\_zombiemode_powerups
// get_valid_powerup() skips it by name ("never drops with regular powerups"), so a zombie kill can never pick it.
// get_valid_powerup() takes the next name of level.zombie_powerup_array (shuffled, walked in order). This module puts a
// second name into that array, "horde_perk": level.zombie_powerups["horde_perk"] is the very struct of "free_perk", so
// powerup_setup() gives the drop retail's bottle (model, hint, name "free_perk") and retail's grab code runs as for
// George's. Only drops that go through get_valid_powerup see it: a zombie kill (powerup_drop), the teleporter special
// drop. Drops made with a name (the dog round's max ammo, the thief, a quantum bomb, George) and the monkey round's
// (level.zombie_powerup_ape) are left as they are. The grab gives every living player one perk they do not have yet
// (_zombiemode_perks give_random_perk, a machine of the map), nothing if they own them all.
// The power-up has to be registered before maps\_zombiemode_powerups::init_powerups (add_zombie_powerup skips what is
// not included): maps\horde\_horde::main_start does that.
// Dvars:
//   horde_perk_drop  15 (default) percent of the zombie drops that are a free-perk bottle. 100 = every zombie drop (the
//                    names a zombie can drop are taken out of the array), 0 = off (retail). Between, "horde_perk" gets as many
//                    slots as that share of the plain names needs, so the share is approximate (it also depends on which of
//                    carpenter / fire sale / revive all / death machine retail lets drop right then). Read twice a second,
//                    so it can be changed while playing. A drop is a bottle only while a living player lacks a perk a
//                    machine of the map sells, and never with +set noperks 1 (mods/noperks has no machines).
//   horde_perk_test  0 (default). A test aid: 1 = 10 s after the players spawn, drop a nuke at the first player, swap it
//                    for a bottle and log how many perks the player had before and 3 s after ("horde_perk: test ...").

init()
{
	if ( GetDvar( #"horde_perk_drop" ) == "" )
	{
		SetDvar( "horde_perk_drop", "15" );
	}
	level thread horde_perk_mix_think();
	level thread horde_perk_log_drops();
	level thread horde_perk_test();
}


horde_perk_log( msg )
{
	LogPrint( "horde_perk: " + msg + "\n" );
}


// every power-up drop, with its name, for the log ("free_perk" = a bottle)
horde_perk_log_drops()
{
	level endon( "end_game" );
	for ( ;; )
	{
		level waittill( "powerup_dropped", powerup );
		if ( IsDefined( powerup ) && IsDefined( powerup.powerup_name ) )
		{
			horde_perk_log( "drop " + powerup.powerup_name );
		}
	}
}


// the names retail's get_valid_powerup() can hand to a zombie drop (the others in the array are always skipped there)
horde_perk_regular( name )
{
	switch ( name )
	{
	case "nuke":
	case "insta_kill":
	case "double_points":
	case "full_ammo":
	case "carpenter":
	case "fire_sale":
	case "all_revive":
	case "minigun":
		return true;
	}
	return false;
}


horde_perk_mix_think()
{
	level endon( "end_game" );
	flag_wait( "all_players_spawned" );
	wait 1;	// the other mods have done their own edits of the array by now
	if ( !IsDefined( level.zombie_powerups ) || !IsDefined( level.zombie_powerups["free_perk"] ) || !IsDefined( level.zombie_powerup_array ) )
	{
		horde_perk_log( "free_perk is not registered: no bottle drops" );
		return;
	}
	level.zombie_powerups["horde_perk"] = level.zombie_powerups["free_perk"];
	base = [];
	for ( i = 0; i < level.zombie_powerup_array.size; i++ )
	{
		base[base.size] = level.zombie_powerup_array[i];
	}
	last = -1;
	for ( ;; )
	{
		share = GetDvarInt( #"horde_perk_drop" );
		if ( share > 100 )
		{
			share = 100;
		}
		if ( share < 0 || GetDvarInt( #"noperks" ) != 0 || !horde_perk_wanted() )
		{
			share = 0;
		}
		if ( share != last )
		{
			horde_perk_set_mix( base, share );
			last = share;
		}
		wait 0.5;
	}
}


// level.zombie_powerup_array for a share of bottles (0 = as retail built it)
horde_perk_set_mix( base, share )
{
	mix = [];
	plain = 0;
	for ( i = 0; i < base.size; i++ )
	{
		if ( horde_perk_regular( base[i] ) )
		{
			plain++;
			if ( share >= 100 )
			{
				continue;	// every zombie drop is a bottle: nothing else may be picked
			}
		}
		mix[mix.size] = base[i];
	}
	slots = 0;
	if ( share >= 100 )
	{
		slots = 1;
	}
	else if ( share > 0 )
	{
		if ( plain < 1 )
		{
			plain = 1;
		}
		slots = Int( share * plain / ( 100 - share ) + 0.5 );
		if ( slots < 1 )
		{
			slots = 1;
		}
	}
	for ( i = 0; i < slots; i++ )
	{
		mix[mix.size] = "horde_perk";
	}
	level.zombie_powerup_array = array_randomize( mix );
	level.zombie_powerup_index = 0;
	horde_perk_log( "drop share " + share + " percent: " + slots + " bottle slots in " + mix.size + " (" + plain + " plain names)" );
}


horde_perk_swap( powerup )
{
	powerup.fx = undefined;
	powerup maps\_zombiemode_powerups::powerup_setup( "free_perk" );
}


// somebody alive lacks a perk that a machine of this map sells (give_random_perk: the "zombie_vending" triggers)
horde_perk_wanted()
{
	vending = GetEntArray( "zombie_vending", "targetname" );
	if ( vending.size == 0 )
	{
		return false;
	}
	players = get_players();
	for ( i = 0; i < players.size; i++ )
	{
		if ( players[i].sessionstate == "spectator" || players[i] maps\_laststand::player_is_in_laststand() )
		{
			continue;
		}
		for ( j = 0; j < vending.size; j++ )
		{
			if ( !players[i] HasPerk( vending[j].script_noteworthy ) )
			{
				return true;
			}
		}
	}
	return false;
}


horde_perk_count( player )
{
	vending = GetEntArray( "zombie_vending", "targetname" );
	n = 0;
	for ( i = 0; i < vending.size; i++ )
	{
		if ( player HasPerk( vending[i].script_noteworthy ) )
		{
			n++;
		}
	}
	return n;
}


horde_perk_test()
{
	if ( GetDvar( #"horde_perk_test" ) == "" )
	{
		SetDvar( "horde_perk_test", "0" );
	}
	level endon( "end_game" );
	flag_wait( "all_players_spawned" );
	if ( GetDvarInt( #"horde_perk_test" ) == 0 )
	{
		return;
	}
	wait 10;
	player = get_players()[0];
	before = horde_perk_count( player );
	nuke = Spawn( "script_model", player.origin + ( 0, 0, 40 ) );
	nuke maps\_zombiemode_powerups::powerup_setup( "nuke" );
	nuke thread maps\_zombiemode_powerups::powerup_timeout();
	nuke thread maps\_zombiemode_powerups::powerup_wobble();
	horde_perk_swap( nuke );
	nuke thread maps\_zombiemode_powerups::powerup_grab();
	wait 3;
	horde_perk_log( "test perks before " + before + " after " + horde_perk_count( player ) + " item still there " + IsDefined( nuke ) );
}
