/*
	Copyright (C) 2003 - 2025
	by David White <dave@whitevine.net>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

/**
 * @file
 * Calculate & analyze attacks of the default ai
 */

#include "ai/manager.hpp"
#include "ai/default/contexts.hpp"
#include "ai/actions.hpp"
#include "ai/composite/contexts.hpp"

#include "actions/attack.hpp"
#include "attack_prediction.hpp"
#include "game_config.hpp"
#include "log.hpp"
#include "map/map.hpp"
#include "team.hpp"
#include "units/unit.hpp"
#include "resources.hpp"
#include "game_board.hpp"

static lg::log_domain log_ai("ai/attack");
#define LOG_AI LOG_STREAM(info, log_ai)
#define ERR_AI LOG_STREAM(err, log_ai)

namespace ai {

void attack_analysis::analyze(const gamemap& map, unit_map& units,
                              const readonly_context& ai_obj,
                              const move_map& dstsrc, const move_map& srcdst,
                              const move_map& enemy_dstsrc, double aggression)
{
	const unit_map::const_iterator defend_it = units.find(target);
	assert(defend_it != units.end());

	// See if the target is a threat to our leader or an ally's leader.
	const auto adj = get_adjacent_tiles(target);
	std::size_t tile;
	for(tile = 0; tile < adj.size(); ++tile) {
		const unit_map::const_iterator leader = units.find(adj[tile]);
		if(leader != units.end() && leader->can_recruit() && !ai_obj.current_team().is_enemy(leader->side())) {
			break;
		}
	}

	leader_threat = (tile != 6);
	uses_leader = false;

	target_value = defend_it->cost();
	target_value += (static_cast<double>(defend_it->experience())/
	                 static_cast<double>(defend_it->max_experience()))*target_value;
	target_starting_damage = defend_it->max_hitpoints() -
	                         defend_it->hitpoints();

	// Calculate the 'alternative_terrain_quality' -- the best possible defensive values
	// the attacking units could hope to achieve if they didn't attack and moved somewhere.
	// This is used for comparative purposes, to see just how vulnerable the AI is
	// making itself.
	alternative_terrain_quality = 0.0;
	double cost_sum = 0.0;
	for(std::size_t i = 0; i != movements.size(); ++i) {
		const unit_map::const_iterator att = units.find(movements[i].first);
		const double cost = att->cost();
		cost_sum += cost;
		alternative_terrain_quality += cost*ai_obj.best_defensive_position(movements[i].first,dstsrc,srcdst,enemy_dstsrc).chance_to_hit;
	}
	alternative_terrain_quality /= cost_sum*100;

	avg_damage_inflicted = 0.0;
	avg_damage_taken = 0.0;
	resources_used = 0.0;
	terrain_quality = 0.0;
	avg_losses = 0.0;
	chance_to_kill = 0.0;

	double def_avg_experience = 0.0;
	double first_chance_kill = 0.0;

	double prob_dead_already = 0.0;
	assert(!movements.empty());
	std::vector<std::pair<map_location,map_location>>::const_iterator m;

	std::unique_ptr<battle_context> bc(nullptr);
	std::unique_ptr<battle_context> old_bc(nullptr);

	const combatant *prev_def = nullptr;

	for (m = movements.begin(); m != movements.end(); ++m) {
		// We fix up units map to reflect what this would look like.
		unit_ptr up = units.extract(m->first);
		up->set_location(m->second);
		units.insert(up);
		double m_aggression = aggression;

		if (up->can_recruit()) {
			uses_leader = true;
			// FIXME: suokko's r29531 omitted this line
			leader_threat = false;
			m_aggression = ai_obj.get_leader_aggression();
		}

		bool from_cache = false;

		// Swap the two context pointers. old_bc should be null at this point, so bc is cleared
		// and old_bc takes ownership of the context pointer. This allows prev_def to remain
		// valid until it's reassigned.
		old_bc.swap(bc);

		// This cache is only about 99% correct, but speeds up evaluation by about 1000 times.
		// We recalculate when we actually attack.
		const readonly_context::unit_stats_cache_t::key_type cache_key = std::pair(target, &up->type());
		const readonly_context::unit_stats_cache_t::iterator usc = ai_obj.unit_stats_cache().find(cache_key);
		// Just check this attack is valid for this attacking unit (may be modified)
		if (usc != ai_obj.unit_stats_cache().end() &&
				usc->second.first.attack_num <
				static_cast<int>(up->attacks().size())) {

			from_cache = true;
			bc.reset(new battle_context(usc->second.first, usc->second.second));
		} else {
			bc.reset(new battle_context(units, m->second, target, -1, -1, m_aggression, prev_def));
		}
		const combatant &att = bc->get_attacker_combatant(prev_def);
		const combatant &def = bc->get_defender_combatant(prev_def);

		prev_def = &bc->get_defender_combatant(prev_def);

		// We no longer need the old context since prev_def has been reassigned.
		old_bc.reset(nullptr);

		if ( !from_cache ) {
			ai_obj.unit_stats_cache().emplace(cache_key, std::pair(
				bc->get_attacker_stats(),
				bc->get_defender_stats()
			));
		}

		// Note we didn't fight at all if defender is already dead.
		double prob_fought = (1.0 - prob_dead_already);

		double prob_killed = def.hp_dist[0] - prob_dead_already;
		prob_dead_already = def.hp_dist[0];

		double prob_died = att.hp_dist[0];
		double prob_survived = (1.0 - prob_died) * prob_fought;

		double cost = up->cost();
		const bool on_village = map.is_village(m->second);
		// Up to double the value of a unit based on experience
		cost += (static_cast<double>(up->experience()) / up->max_experience())*cost;
		resources_used += cost;
		avg_losses += cost * prob_died;

		// add half of cost for poisoned unit so it might get chance to heal
		avg_losses += cost * up->get_state(unit::STATE_POISONED) /2;

		if (!bc->get_defender_stats().is_poisoned) {
			avg_damage_inflicted += game_config::poison_amount * 2 * bc->get_defender_combatant().poisoned * (1 - prob_killed);
		}

		// Double reward to emphasize getting onto villages if they survive.
		if (on_village) {
			avg_damage_taken -= game_config::poison_amount*2 * prob_survived;
		}

		terrain_quality += (static_cast<double>(bc->get_defender_stats().chance_to_hit)/100.0)*cost * (on_village ? 0.5 : 1.0);

		double advance_prob = 0.0;
		// The reward for advancing a unit is to get a 'negative' loss of that unit
		if (!up->advances_to().empty()) {
			int xp_for_advance = up->experience_to_advance();

			// See bug #6272... in some cases, unit already has got enough xp to advance,
			// but hasn't (bug elsewhere?).  Can cause divide by zero.
			if (xp_for_advance == 0)
				xp_for_advance = 1;

			int fight_xp = game_config::combat_xp(defend_it->level());
			int kill_xp = game_config::kill_xp(fight_xp);

			if (fight_xp >= xp_for_advance) {
				advance_prob = prob_fought;
				avg_losses -= up->cost() * prob_fought;
			} else if (kill_xp >= xp_for_advance) {
				advance_prob = prob_killed;
				avg_losses -= up->cost() * prob_killed;
				// The reward for getting a unit closer to advancement
				// (if it didn't advance) is to get the proportion of
				// remaining experience needed, and multiply it by
				// a quarter of the unit cost.
				// This will cause the AI to heavily favor
				// getting xp for close-to-advance units.
				avg_losses -= up->cost() * 0.25 *
					fight_xp * (prob_fought - prob_killed)
					/ xp_for_advance;
			} else {
				avg_losses -= up->cost() * 0.25 *
					(kill_xp * prob_killed + fight_xp * (prob_fought - prob_killed))
					/ xp_for_advance;
			}

			// The reward for killing with a unit that plagues
			// is to get a 'negative' loss of that unit.
			if (bc->get_attacker_stats().plagues) {
				avg_losses -= prob_killed * up->cost();
			}
		}

		// If we didn't advance, we took this damage.
		avg_damage_taken += (up->hitpoints() - att.average_hp()) * (1.0 - advance_prob);

		int fight_xp = game_config::combat_xp(up->level());
		int kill_xp = game_config::kill_xp(fight_xp);
		def_avg_experience += fight_xp * (1.0 - att.hp_dist[0]) + kill_xp * att.hp_dist[0];
		if (m == movements.begin()) {
			first_chance_kill = def.hp_dist[0];
		}
	}

	if (!defend_it->advances_to().empty() &&
		def_avg_experience >= defend_it->experience_to_advance()) {
		// It's likely to advance: only if we can kill with first blow.
		chance_to_kill = first_chance_kill;
		// Negative average damage (it will advance).
		avg_damage_inflicted += defend_it->hitpoints() - defend_it->max_hitpoints();
	} else {
		chance_to_kill = prev_def->hp_dist[0];
		avg_damage_inflicted += defend_it->hitpoints() - prev_def->average_hp(map.gives_healing(defend_it->get_location()));
	}

	terrain_quality /= resources_used;

	// Restore the units to their original positions.
	for (m = movements.begin(); m != movements.end(); ++m) {
		units.move(m->second, m->first);
	}
}

bool attack_analysis::attack_close(const map_location& loc) const
{
	std::set<map_location> &attacks = manager::get_singleton().get_ai_info().recent_attacks;
	for(std::set<map_location>::const_iterator i = attacks.begin(); i != attacks.end(); ++i) {
		if(distance_between(*i,loc) < 4) {
			return true;
		}
	}

	return false;
}


double attack_analysis::rating(double aggression, const readonly_context& ai_obj) const
{
	if(leader_threat) {
		aggression = 1.0;
	}

	if(uses_leader) {
		aggression = ai_obj.get_leader_aggression();
	}

	double value = chance_to_kill*target_value - avg_losses*(1.0-aggression);

	if(terrain_quality > alternative_terrain_quality) {
		// This situation looks like it might be a bad move:
		// we are moving our attackers out of their optimal terrain
		// into sub-optimal terrain.
		// Calculate the 'exposure' of our units to risk.

		const double exposure_mod = uses_leader ? 2.0 : ai_obj.get_caution();
		const double exposure = exposure_mod*resources_used*(terrain_quality - alternative_terrain_quality)*vulnerability/std::max<double>(0.01,support);
		LOG_AI << "attack option has base value " << value << " with exposure " << exposure << ": "
			<< vulnerability << "/" << support << " = " << (vulnerability/std::max<double>(support,0.1));
		value -= exposure*(1.0-aggression);
	}

	// Prefer to attack already damaged targets.
	value += ((target_starting_damage/3 + avg_damage_inflicted) - (1.0-aggression)*avg_damage_taken)/10.0;

	// If the unit is surrounded and there is no support,
	// or if the unit is surrounded and the average damage is 0,
	// the unit skips its sanity check and tries to break free as good as possible.
	if(!is_surrounded || (support != 0 && avg_damage_taken != 0))
	{
		// Sanity check: if we're putting ourselves at major risk,
		// and have no chance to kill, and we're not aiding our allies
		// who are also attacking, then don't do it.
		if(vulnerability > 50.0 && vulnerability > support*2.0
		&& chance_to_kill < 0.02 && aggression < 0.75
		&& !attack_close(target)) {
			return -1.0;
		}
	}

	if(!leader_threat && vulnerability*terrain_quality > 0.0 && support != 0) {
		value *= support/(vulnerability*terrain_quality);
	}

	value /= ((resources_used/2) + (resources_used/2)*terrain_quality);

	if(leader_threat) {
		value *= 5.0;
	}

	LOG_AI << "attack on " << target << ": attackers: " << movements.size()
		<< " value: " << value << " chance to kill: " << chance_to_kill
		<< " damage inflicted: " << avg_damage_inflicted
		<< " damage taken: " << avg_damage_taken
		<< " vulnerability: " << vulnerability
		<< " support: " << support
		<< " quality: " << terrain_quality
		<< " alternative quality: " << alternative_terrain_quality;

	return value;
}

} //end of namespace ai
