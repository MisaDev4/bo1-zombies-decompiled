#include maps\_utility;
#include common_scripts\utility;
#include maps\_zombiemode_utility;

// HORDE MOD - not part of the original game. Free-perk bottles as ordinary power-up drops. Retail only drops
// "free_perk" from Call of the Dead's George Romero (and the teleporter special drop): maps\_zombiemode_powerups
// get_valid_powerup() skips it by name ("never drops with regular powerups"), so a zombie kill can never pick it.
// A regular zombie drop (maps\_zombiemode_powerups powerup_drop) is picked and set up there, then announced with
// level notify( "powerup_dropped", powerup ). Here a share of those drops is turned into the free-perk bottle
// (powerup_setup( "free_perk" ) again: model, name, hint; the grab, wobble and timeout threads read the name when they
// need it). The bottle is retail's: grabbing it gives every living player one perk they do not have yet
// (_zombiemode_perks give_random_perk, a machine of the map), nothing if they own them all.
// The power-up has to be registered before maps\_zombiemode_powerups::init_powerups (add_zombie_powerup skips what is
// not included): maps\horde\_horde::main_start does that.
// Dvar:
//   horde_perk_drop  15 (default) percent of the regular power-up drops that become a free-perk bottle. 0 = off (retail).
//                    Can be changed while playing. Not used with +set noperks 1 (mods/noperks has no machines).
//   horde_perk_test  0 (default). A test aid: 1 = 10 s after the players spawn, drop a nuke at the first player, swap it
//                    like a regular drop and log how many perks the player had before and 3 s after ("horde_perk: test ...").
// Only the plain drops are swapped (nuke, insta-kill, double points, max ammo, carpenter, fire sale, revive all): never the
// weapon ones (death machine, tesla, random weapon), never a drop another script made on purpose (George, the thief, a
// sandbox pad: those go through specific_powerup_drop, which does not count in level.powerup_drop_count), and never when
// no living player lacks a perk a machine of the map sells.

init()
{
	if ( GetDvar( #"horde_perk_drop" ) == "" )
	{
		SetDvar( "horde_perk_drop", "15" );
	}
	level thread horde_perk_drop_think();
	level thread horde_perk_test();
}


horde_perk_log( msg )
{
	LogPrint( "horde_perk: " + msg + "\n" );
}


horde_perk_drop_think()
{
	level endon( "end_game" );
	flag_wait( "all_players_spawned" );
	last_key = -1;
	swapped = 0;
	regular = 0;
	for ( ;; )
	{
		level waittill( "powerup_dropped", powerup );
		// powerup_drop counts the drop (level.powerup_drop_count, reset every round) before it notifies; a drop made by
		// specific_powerup_drop leaves the count alone
		key = level.round_number * 1000 + level.powerup_drop_count;
		if ( key == last_key )
		{
			continue;
		}
		last_key = key;
		regular++;
		if ( !IsDefined( powerup ) || !IsDefined( powerup.powerup_name ) )
		{
			continue;
		}
		chance = GetDvarInt( #"horde_perk_drop" );
		if ( chance <= 0 || GetDvarInt( #"noperks" ) != 0 )
		{
			continue;
		}
		if ( !IsDefined( level.zombie_powerups ) || !IsDefined( level.zombie_powerups["free_perk"] ) )
		{
			continue;
		}
		if ( !horde_perk_swappable( powerup.powerup_name ) || !horde_perk_wanted() )
		{
			continue;
		}
		if ( RandomInt( 100 ) >= chance )
		{
			continue;
		}
		old = powerup.powerup_name;
		horde_perk_swap( powerup );
		swapped++;
		horde_perk_log( "drop " + old + " -> free_perk (" + swapped + " of " + regular + " regular drops)" );
	}
}


horde_perk_swap( powerup )
{
	powerup.fx = undefined;
	powerup maps\_zombiemode_powerups::powerup_setup( "free_perk" );
}


horde_perk_swappable( name )
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
		return true;
	}
	return false;
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
