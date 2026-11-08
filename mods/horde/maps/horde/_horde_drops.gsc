#include maps\_utility;
#include common_scripts\utility;
#include maps\_zombiemode_utility;

// HORDE MOD - not part of the original game. How often zombies drop power-ups. Retail (maps\_zombiemode_powerups
// watch_for_drop, powerup_drop; the numbers are in the zombie vars table, Der Riese: 2000 / 4 / x1.14):
//   - every time the team's total score has grown by a gap, the next zombie kill drops a power-up. The first gap is
//     zombie_powerup_drop_increment (2000) on top of the start score; after each drop the increment is multiplied by 1.14,
//     so the gaps grow: 2000, 2280, 2599, ...
//   - at most zombie_powerup_drop_max_per_round (4) drops per round.
// Dvars (-1 = retail; each is read while playing):
//   horde_drop_max     most drops per round (retail 4). 0 = no drops.
//   horde_drop_step    score between drops, the first gap (retail 2000)
//   horde_drop_growth  what the gap is multiplied by after each drop (retail 1.14); 1 = the same gap every time
// Retail multiplies the increment itself, inside watch_for_drop, right when it hands out a drop, and works out the next
// target from the result. So the variable is kept at (wanted next gap) / 1.14 between drops: retail's own multiplication
// then lands on the wanted gap. With every dvar at -1 the variable is never touched.

init()
{
	if ( GetDvar( #"horde_drop_max" ) == "" )
	{
		SetDvar( "horde_drop_max", "-1" );
	}
	if ( GetDvar( #"horde_drop_step" ) == "" )
	{
		SetDvar( "horde_drop_step", "-1" );
	}
	if ( GetDvar( #"horde_drop_growth" ) == "" )
	{
		SetDvar( "horde_drop_growth", "-1" );
	}
	level thread horde_drop_max_think();
	level thread horde_drop_step_think();
}


horde_drop_log( msg )
{
	LogPrint( "horde_drop: " + msg + "\n" );
}


horde_drop_vars_wait()
{
	while ( !IsDefined( level.zombie_vars ) || !IsDefined( level.zombie_vars["zombie_powerup_drop_increment"] )
		|| !IsDefined( level.zombie_vars["zombie_powerup_drop_max_per_round"] ) )
	{
		wait 0.05;
	}
}


horde_drop_max_think()
{
	level endon( "end_game" );
	horde_drop_vars_wait();
	retail = level.zombie_vars["zombie_powerup_drop_max_per_round"];
	last = retail;
	for ( ;; )
	{
		want = retail;
		if ( GetDvarInt( #"horde_drop_max" ) >= 0 )
		{
			want = GetDvarInt( #"horde_drop_max" );
		}
		if ( want != last )
		{
			horde_drop_log( "drops per round " + last + " -> " + want );
			last = want;
		}
		level.zombie_vars["zombie_powerup_drop_max_per_round"] = want;
		wait 0.5;
	}
}


// the gap before drop k+1 (k = drops so far) is step * growth^(k+1); the variable is held at that / 1.14
horde_drop_hold( retail, k )
{
	step = retail;
	if ( GetDvarInt( #"horde_drop_step" ) > 0 )
	{
		step = GetDvarInt( #"horde_drop_step" );
	}
	growth = 1.14;
	if ( GetDvarFloat( #"horde_drop_growth" ) >= 0.1 )
	{
		growth = GetDvarFloat( #"horde_drop_growth" );
	}
	gap = step;
	for ( i = 0; i <= k; i++ )
	{
		gap = gap * growth;
	}
	// gap is now step * growth^(k+1)
	return gap / 1.14;
}


horde_drop_abs( v )
{
	if ( v < 0 )
	{
		return 0 - v;
	}
	return v;
}


horde_drop_step_think()
{
	level endon( "end_game" );
	horde_drop_vars_wait();
	inc = "zombie_powerup_drop_increment";
	retail = level.zombie_vars[inc];
	k = 0;
	if ( !flag( "begin_spawning" ) )
	{
		// the first gap: watch_for_drop reads the increment once, when spawning begins
		if ( GetDvarInt( #"horde_drop_step" ) > 0 )
		{
			level.zombie_vars[inc] = GetDvarInt( #"horde_drop_step" );
		}
		flag_wait( "begin_spawning" );
		wait 0.2;
	}
	mine = level.zombie_vars[inc];
	lastkey = "";
	for ( ;; )
	{
		cur = level.zombie_vars[inc];
		if ( horde_drop_abs( cur - mine ) > 0.5 )
		{
			// retail multiplied it: a drop was handed out
			k++;
			horde_drop_log( "drop " + k + " handed out, next gap " + Int( cur ) );
		}
		want = horde_drop_hold( retail, k );
		key = GetDvar( #"horde_drop_step" ) + "/" + GetDvar( #"horde_drop_growth" );
		if ( key != lastkey )
		{
			lastkey = key;
			horde_drop_log( "step " + GetDvar( #"horde_drop_step" ) + " growth " + GetDvar( #"horde_drop_growth" ) + " (-1 = retail " + Int( retail ) + " x1.14), after " + k + " drops" );
		}
		if ( horde_drop_abs( cur - want ) > 0.5 )
		{
			level.zombie_vars[inc] = want;
		}
		mine = level.zombie_vars[inc];
		wait 0.05;
	}
}
