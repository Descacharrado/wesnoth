/*
	Copyright (C) 2008 - 2025
	by Mark de Wever <koraq@xs4all.nl>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "gui/dialogs/multiplayer/mp_create_game.hpp"

#include "formatter.hpp"
#include "formula/string_utils.hpp"
#include "game_config.hpp"
#include "game_config_manager.hpp"
#include "gettext.hpp"
#include "gui/auxiliary/field.hpp"
#include "gui/dialogs/simple_item_selector.hpp"
#include "gui/dialogs/transient_message.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/image.hpp"
#include "gui/widgets/listbox.hpp"
#include "gui/widgets/menu_button.hpp"
#include "gui/widgets/minimap.hpp"
#include "gui/widgets/slider.hpp"
#include "gui/widgets/stacked_widget.hpp"
#include "gui/widgets/status_label_helper.hpp"
#include "gui/widgets/text_box.hpp"
#include "gui/widgets/toggle_button.hpp"
#include "gui/widgets/toggle_panel.hpp"
#include "log.hpp"
#include "map_settings.hpp"
#include "preferences/preferences.hpp"
#include "save_index.hpp"
#include "savegame.hpp"
#include "tod_manager.hpp"

#include <boost/algorithm/string.hpp>

static lg::log_domain log_mp_create("mp/create");

#define DBG_MP LOG_STREAM(debug, log_mp_create)
#define WRN_MP LOG_STREAM(warn, log_mp_create)
#define ERR_MP LOG_STREAM(err, log_mp_create)

namespace gui2::dialogs
{

// Special retval value for loading a game
static const int LOAD_GAME = 100;

REGISTER_DIALOG(mp_create_game)

mp_create_game::mp_create_game(saved_game& state, bool local_mode)
	: modal_dialog(window_id())
	, create_engine_(state)
	, options_manager_()
	, selected_game_index_(-1)
	, selected_rfm_index_(-1)
	, use_map_settings_(register_bool( "use_map_settings", true,
		[]() {return prefs::get().mp_use_map_settings();},
		[](bool v) {prefs::get().set_mp_use_map_settings(v);},
		std::bind(&mp_create_game::update_map_settings, this)))
	, fog_(register_bool("fog", true,
		[]() {return prefs::get().mp_fog();},
		[](bool v) {prefs::get().set_mp_fog(v);}))
	, shroud_(register_bool("shroud", true,
		[]() {return prefs::get().mp_shroud();},
		[](bool v) {prefs::get().set_mp_shroud(v);}))
	, start_time_(register_bool("random_start_time", true,
		[]() {return prefs::get().mp_random_start_time();},
		[](bool v) {prefs::get().set_mp_random_start_time(v);}))
	, time_limit_(register_bool("time_limit", true,
		[]() {return prefs::get().mp_countdown();},
		[](bool v) {prefs::get().set_mp_countdown(v);},
		std::bind(&mp_create_game::update_map_settings, this)))
	, shuffle_sides_(register_bool("shuffle_sides", true,
		[]() {return prefs::get().shuffle_sides();},
		[](bool v) {prefs::get().set_shuffle_sides(v);}))
	, observers_(register_bool("observers", true,
		[]() {return prefs::get().allow_observers();},
		[](bool v) {prefs::get().set_allow_observers(v);}))
	, strict_sync_(register_bool("strict_sync", true))
	, private_replay_(register_bool("private_replay", true))
	, turns_(register_integer("turn_count", true,
		[]() {return prefs::get().mp_turns();},
		[](int v) {prefs::get().set_mp_turns(v);}))
	, gold_(register_integer("village_gold", true,
		[]() {return prefs::get().village_gold();},
		[](int v) {prefs::get().set_village_gold(v);}))
	, support_(register_integer("village_support", true,
		[]() {return prefs::get().village_support();},
		[](int v) {prefs::get().set_village_support(v);}))
	, experience_(register_integer("experience_modifier", true,
		[]() {return prefs::get().xp_modifier();},
		[](int v) {prefs::get().set_xp_modifier(v);}))
	, init_turn_limit_(register_integer("init_turn_limit", true,
		[]() {return prefs::get().countdown_init_time().count();},
		[](int v) {prefs::get().set_countdown_init_time(std::chrono::seconds{v});}))
	, turn_bonus_(register_integer("turn_bonus", true,
		[]() {return prefs::get().countdown_turn_bonus().count();},
		[](int v) {prefs::get().set_countdown_turn_bonus(std::chrono::seconds{v});}))
	, reservoir_(register_integer("reservoir", true,
		[]() {return prefs::get().countdown_reservoir_time().count();},
		[](int v) {prefs::get().set_countdown_reservoir_time(std::chrono::seconds{v});}))
	, action_bonus_(register_integer("action_bonus", true,
		[]() {return prefs::get().countdown_action_bonus().count();},
		[](int v) {prefs::get().set_countdown_action_bonus(std::chrono::seconds{v});}))
	, mod_list_()
	, eras_menu_button_()
	, local_mode_(local_mode)
{
	level_types_ = {
		{level_type::type::scenario, _("Scenarios")},
		{level_type::type::campaign, _("Multiplayer Campaigns")},
		{level_type::type::sp_campaign, _("Singleplayer Campaigns")},
		{level_type::type::user_map, _("Custom Maps")},
		{level_type::type::user_scenario, _("Custom Scenarios")},
		{level_type::type::random_map, _("Random Maps")},
		{level_type::type::preset, _("Presets")},
	};

	utils::erase_if(level_types_, [this](level_type_info& type_info) {
		return create_engine_.get_levels_by_type_unfiltered(type_info.first).empty();
	});

	set_show_even_without_video(true);

	create_engine_.init_active_mods();

	create_engine_.get_state().clear();
	create_engine_.get_state().classification().type = campaign_type::type::multiplayer;

	// Need to set this in the constructor, pre_show() is too late
	set_allow_plugin_skip(false);
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void mp_create_game::quick_mp_setup(saved_game& state, const config presets)
{
	// from constructor
	ng::create_engine create(state);
	create.init_active_mods();
	create.get_state().clear();
	create.get_state().classification().type = campaign_type::type::multiplayer;

	// from pre_show
	create.set_current_level_type(level_type::type::scenario);
	const auto& levels = create.get_levels_by_type(level_type::type::scenario);
	for(std::size_t i = 0; i < levels.size(); i++) {
		if(levels[i]->id() == presets["scenario"].str()) {
			create.set_current_level(i);
		}
	}

	create.set_current_era_id(presets["era"]);

	// from post_show
	create.prepare_for_era_and_mods();
	create.prepare_for_scenario();
	create.get_parameters();
	create.prepare_for_new_level();

	mp_game_settings& params = create.get_state().mp_settings();
	params.mp_scenario = presets["scenario"].str();
	params.use_map_settings = true;
	params.num_turns = presets["turn_count"].to_int(-1);
	params.village_gold = presets["village_gold"].to_int();
	params.village_support = presets["village_support"].to_int();
	params.xp_modifier = presets["experience_modifier"].to_int();
	params.random_start_time = presets["random_start_time"].to_bool();
	params.fog_game = presets["fog"].to_bool();
	params.shroud_game = presets["shroud"].to_bool();

	// write to scenario
	// queue games are supposed to all use the same settings, not be modified by the user
	// can be removed later if we jump straight from the lobby into a game instead of going to the staging screen to wait for other players to join
	config& scenario = create.get_state().get_starting_point();

	if(params.random_start_time) {
		if(!tod_manager::is_start_ToD(scenario["random_start_time"])) {
			scenario["random_start_time"] = true;
		}
	} else {
		scenario["random_start_time"] = false;
	}

	scenario["experience_modifier"] = params.xp_modifier;
	scenario["turns"] = params.num_turns;

	for(config& side : scenario.child_range("side")) {
		side["controller_lock"] = false;
		side["team_lock"] = true;
		side["gold_lock"] = true;
		side["income_lock"] = true;

		side["fog"] = params.fog_game;
		side["shroud"] = params.shroud_game;
		side["village_gold"] = params.village_gold;
		side["village_support"] = params.village_support;
	}

	params.mp_countdown = presets["countdown"].to_bool();
	params.mp_countdown_init_time = std::chrono::seconds{presets["countdown_init_time"].to_int()};
	params.mp_countdown_turn_bonus = std::chrono::seconds{presets["countdown_turn_bonus"].to_int()};
	params.mp_countdown_reservoir_time = std::chrono::seconds{presets["countdown_reservoir_time"].to_int()};
	params.mp_countdown_action_bonus = std::chrono::seconds{presets["countdown_action_bonus"].to_int()};

	params.allow_observers = true;
	params.private_replay = false;
	create.get_state().classification().oos_debug = false;
	params.shuffle_sides = presets["shuffle_sides"].to_bool();

	params.mode = random_faction_mode::type::no_mirror;
	params.name = settings::game_name_default();
}

void mp_create_game::pre_show()
{
	find_widget<text_box>("game_name").set_value(local_mode_ ? "" : settings::game_name_default());

	connect_signal_mouse_left_click(
		find_widget<button>("random_map_regenerate"),
		std::bind(&mp_create_game::regenerate_random_map, this));

	connect_signal_mouse_left_click(
		find_widget<button>("random_map_settings"),
		std::bind(&mp_create_game::show_generator_settings, this));

	connect_signal_mouse_left_click(
		find_widget<button>("load_game"),
		std::bind(&mp_create_game::load_game_callback, this));

	connect_signal_mouse_left_click(
		find_widget<button>("save_preset"),
		std::bind(&mp_create_game::save_preset, this));

	// Custom dialog close hook
	set_exit_hook(window::exit_hook::ok_only, [this] { return dialog_exit_hook(); });

	//
	// Set up the options manager. Needs to be done before selecting an initial tab
	//
	options_manager_.reset(new mp_options_helper(*this, create_engine_));

	//
	// Set up filtering
	//
	connect_signal_notify_modified(find_widget<slider>("num_players"),
		std::bind(&mp_create_game::on_filter_change<slider>, this, "num_players", true));

	text_box& filter = find_widget<text_box>("game_filter");

	filter.on_modified([this](const auto&) { on_filter_change<text_box>("game_filter", true); });

	// Note this cannot be in the keyboard chain or it will capture focus from other text boxes
	keyboard_capture(&filter);

	//
	// Set up game types menu_button
	//
	std::vector<config> game_types;
	for(level_type_info& type_info : level_types_) {
		game_types.emplace_back("label", type_info.second);
	}

	if(game_types.empty()) {
		gui2::show_transient_message("", _("No games found."));
		throw game::error(_("No games found."));
	}

	menu_button& game_menu_button = find_widget<menu_button>("game_types");

	// Helper to make sure the initially selected level type is valid
	auto get_initial_type_index = [this]()->int {
		const auto index = utils::ranges::find(level_types_,
			level_type::get_enum(prefs::get().mp_level_type()).value(), &level_type_info::first);

		if(index != level_types_.end()) {
			return std::distance(level_types_.begin(), index);
		}

		return 0;
	};

	int initial_type = get_initial_type_index();
	game_menu_button.set_values(game_types, initial_type);
	find_widget<button>("save_preset").set_active(level_type::get_enum(initial_type) == level_type::type::scenario);

	connect_signal_notify_modified(game_menu_button,
		std::bind(&mp_create_game::update_games_list, this));

	//
	// Set up mods list
	//
	mod_list_ = &find_widget<listbox>("mod_list");

	const auto& activemods = prefs::get().modifications();
	for(const auto& mod : create_engine_.get_extras_by_type(ng::create_engine::MOD)) {
		grid* row_grid = &mod_list_->add_row(widget_data{{ "mod_name", {{ "label", mod->name }}}});

		row_grid->find_widget<toggle_panel>("panel").set_tooltip(mod->description);

		toggle_button& mog_toggle = row_grid->find_widget<toggle_button>("mod_active_state");

		if(std::find(activemods.begin(), activemods.end(), mod->id) != activemods.end()) {
			create_engine_.active_mods().push_back(mod->id);
			mog_toggle.set_value_bool(true);
		}

		connect_signal_notify_modified(mog_toggle, std::bind(&mp_create_game::on_mod_toggle, this, mod->id, &mog_toggle));
	}

	// No mods, hide the header
	if(mod_list_->get_item_count() <= 0) {
		find_widget<styled_widget>("mods_header").set_visible(widget::visibility::invisible);
	}

	//
	// Set up eras menu_button
	//
	eras_menu_button_ = &find_widget<menu_button>("eras");

	std::vector<config> era_names;
	for(const auto& era : create_engine_.get_const_extras_by_type(ng::create_engine::ERA)) {
		era_names.emplace_back("label", era->name, "tooltip", era->description);
	}

	if(era_names.empty()) {
		gui2::show_transient_message("", _("No eras found."));
		throw config::error(_("No eras found"));
	}

	eras_menu_button_->set_values(era_names);

	connect_signal_notify_modified(*eras_menu_button_,
		std::bind(&mp_create_game::on_era_select, this));

	const int era_selection = create_engine_.find_extra_by_id(ng::create_engine::ERA, prefs::get().mp_era());
	if(era_selection >= 0) {
		eras_menu_button_->set_selected(era_selection);
	}

	on_era_select();

	//
	// Set up random faction mode menu_button
	//
	const int initial_index = static_cast<int>(random_faction_mode::get_enum(prefs::get().random_faction_mode()).value_or(random_faction_mode::type::independent));

	menu_button& rfm_menu_button = find_widget<menu_button>("random_faction_mode");
	rfm_menu_button.set_selected(initial_index);

	connect_signal_notify_modified(rfm_menu_button,
		std::bind(&mp_create_game::on_random_faction_mode_select, this));

	on_random_faction_mode_select();

	//
	// Set up the setting status labels
	//
	bind_default_status_label(static_cast<slider&>(*turns_->get_widget()));
	bind_default_status_label(static_cast<slider&>(*gold_->get_widget()));
	bind_default_status_label(static_cast<slider&>(*support_->get_widget()));
	bind_default_status_label(static_cast<slider&>(*experience_->get_widget()));
	bind_default_status_label(static_cast<slider&>(*init_turn_limit_->get_widget()));
	bind_default_status_label(static_cast<slider&>(*turn_bonus_->get_widget()));
	bind_default_status_label(static_cast<slider&>(*reservoir_->get_widget()));
	bind_default_status_label(static_cast<slider&>(*action_bonus_->get_widget()));

	//
	// Timer reset button
	//
	connect_signal_mouse_left_click(
		find_widget<button>("reset_timer_defaults"),
		std::bind(&mp_create_game::reset_timer_settings, this));

	//
	// Disable certain settings if we're playing a local game.
	//
	if(local_mode_) {
		find_widget<text_box>("game_name").set_active(false);
		find_widget<text_box>("game_password").set_active(false);

		observers_->widget_set_enabled(false, false);
		strict_sync_->widget_set_enabled(false, false);
		private_replay_->widget_set_enabled(false, false);
	}

	//
	// Set up tab control
	//
	listbox& tab_bar = find_widget<listbox>("tab_bar");

	connect_signal_notify_modified(tab_bar,
		std::bind(&mp_create_game::on_tab_select, this));

	// Allow the settings stack to find widgets in all pages, regardless of which is selected.
	// This ensures settings (especially game settings) widgets are appropriately updated when
	// a new game is selected, regardless of which settings tab is active at the time.
	find_widget<stacked_widget>("pager").set_find_in_all_layers(true);

	// We call on_tab_select farther down.

	//
	// Main games list
	//
	listbox& list = find_widget<listbox>("games_list");

	connect_signal_notify_modified(list,
		std::bind(&mp_create_game::on_game_select, this));

	add_to_keyboard_chain(&list);

	// This handles the initial game selection as well
	display_games_of_type(level_types_[get_initial_type_index()].first, prefs::get().mp_level());

	// Initial tab selection must be done after game selection so the field widgets are set to their correct active state.
	on_tab_select();

	//
	// Set up the Lua plugin context
	//
	plugins_context_.reset(new plugins_context("Multiplayer Create"));

	plugins_context_->set_callback("create", [this](const config&) { set_retval(retval::OK); }, false);
	plugins_context_->set_callback("quit",   [this](const config&) { set_retval(retval::CANCEL); }, false);
	plugins_context_->set_callback("load",   [this](const config&) { load_game_callback(); }, false);

#define UPDATE_ATTRIBUTE(field, convert) \
	do { if(cfg.has_attribute(#field)) { field##_->set_widget_value(cfg[#field].convert()); } } while(false) \

	plugins_context_->set_callback("update_settings", [this](const config& cfg) {
		UPDATE_ATTRIBUTE(turns, to_int);
		UPDATE_ATTRIBUTE(gold, to_int);
		UPDATE_ATTRIBUTE(support, to_int);
		UPDATE_ATTRIBUTE(experience, to_int);
		UPDATE_ATTRIBUTE(start_time, to_bool);
		UPDATE_ATTRIBUTE(fog, to_bool);
		UPDATE_ATTRIBUTE(shroud, to_bool);
		UPDATE_ATTRIBUTE(time_limit, to_bool);
		UPDATE_ATTRIBUTE(init_turn_limit, to_int);
		UPDATE_ATTRIBUTE(turn_bonus, to_int);
		UPDATE_ATTRIBUTE(reservoir, to_int);
		UPDATE_ATTRIBUTE(action_bonus, to_int);
		UPDATE_ATTRIBUTE(observers, to_bool);
		UPDATE_ATTRIBUTE(strict_sync, to_bool);
		UPDATE_ATTRIBUTE(private_replay, to_bool);
		UPDATE_ATTRIBUTE(shuffle_sides, to_bool);
	}, true);

#undef UPDATE_ATTRIBUTE

	plugins_context_->set_callback("set_name",     [this](const config& cfg) {
		create_engine_.get_state().mp_settings().name = cfg["name"];
	}, true);

	plugins_context_->set_callback("set_password", [this](const config& cfg) {
		create_engine_.get_state().mp_settings().password = cfg["password"];
	}, true);

	plugins_context_->set_callback("select_level", [this](const config& cfg) {
		selected_game_index_ = convert_to_game_filtered_index(cfg["index"].to_int());
		create_engine_.set_current_level(selected_game_index_);
	}, true);

	plugins_context_->set_callback("select_type",  [this](const config& cfg) {
		create_engine_.set_current_level_type(level_type::get_enum(cfg["type"].str()).value_or(level_type::type::scenario)); }, true);

	plugins_context_->set_callback("select_era",   [this](const config& cfg) {
		create_engine_.set_current_era_index(cfg["index"].to_int()); }, true);

	plugins_context_->set_callback("select_mod",   [this](const config& cfg) {
		on_mod_toggle(cfg["id"].str(), nullptr);
	}, true);

	plugins_context_->set_accessor("get_selected", [this](const config&) {
		const ng::level& current_level = create_engine_.current_level();
		return config {
			"id", current_level.id(),
			"name", current_level.name(),
			"icon", current_level.icon(),
			"description", current_level.description(),
			"allow_era_choice", current_level.allow_era_choice(),
			"type", level_type::get_string(create_engine_.current_level_type()),
		};
	});

	plugins_context_->set_accessor("find_level",   [this](const config& cfg) {
		const std::string id = cfg["id"].str();
		auto result = create_engine_.find_level_by_id(id);
		return config {
			"index", result.second,
			"type", level_type::get_string(result.first),
		};
	});

	plugins_context_->set_accessor_int("find_era", [this](const config& cfg) {
		return create_engine_.find_extra_by_id(ng::create_engine::ERA, cfg["id"]);
	});

	plugins_context_->set_accessor_int("find_mod", [this](const config& cfg) {
		return create_engine_.find_extra_by_id(ng::create_engine::MOD, cfg["id"]);
	});
}

void mp_create_game::sync_with_depcheck()
{
	DBG_MP << "sync_with_depcheck: start";

	if(static_cast<int>(create_engine_.current_era_index()) != create_engine_.dependency_manager().get_era_index()) {
		DBG_MP << "sync_with_depcheck: correcting era";
		const int new_era_index = create_engine_.dependency_manager().get_era_index();

		create_engine_.set_current_era_index(new_era_index, true);
		eras_menu_button_->set_value(new_era_index);
	}

	if(create_engine_.current_level().id() != create_engine_.dependency_manager().get_scenario()) {
		DBG_MP << "sync_with_depcheck: correcting scenario";

		// Match scenario and scenario type
		const auto [new_level_type, new_level_index] = create_engine_.find_level_by_id(create_engine_.dependency_manager().get_scenario());
		const bool different_type = new_level_type != create_engine_.current_level_type();

		if(new_level_index != -1) {
			create_engine_.set_current_level_type(new_level_type);
			create_engine_.set_current_level(new_level_index);
			selected_game_index_ = new_level_index;

			auto iter = utils::ranges::find(level_types_, new_level_type, &level_type_info::first);
			find_widget<menu_button>("game_types").set_value(std::distance(level_types_.begin(), iter));

			if(different_type) {
				display_games_of_type(new_level_type, create_engine_.current_level().id());
			} else {
				// This function (or rather on_game_select) might be triggered by a listbox callback, in
				// which case we cannot use display_games_of_type since it destroys the list (and its
				// elements) which might result in segfaults. Instead, we assume that a listbox-triggered
				// sync_with_depcheck call never changes the game type and goes to this branch instead.
				find_widget<listbox>("games_list").select_row(new_level_index);

				// Override the last selection so on_game_select selects the new level
				selected_game_index_ = -1;

				on_game_select();
			}
		}
	}

	if(get_active_mods() != create_engine_.dependency_manager().get_modifications()) {
		DBG_MP << "sync_with_depcheck: correcting modifications";
		set_active_mods(create_engine_.dependency_manager().get_modifications());
	}

	create_engine_.init_active_mods();
	DBG_MP << "sync_with_depcheck: end";
}

template<typename T>
void mp_create_game::on_filter_change(const std::string& id, bool do_select)
{
	create_engine_.apply_level_filter(find_widget<T>(id).get_value());

	listbox& game_list = find_widget<listbox>("games_list");

	boost::dynamic_bitset<> filtered(game_list.get_item_count());
	for(const std::size_t i : create_engine_.get_filtered_level_indices(create_engine_.current_level_type())) {
		filtered[i] = true;
	}

	game_list.set_row_shown(filtered);

	if(do_select) {
		on_game_select();
	}
}

void mp_create_game::on_game_select()
{
	const int selected_game = find_widget<listbox>("games_list").get_selected_row();

	if(selected_game == selected_game_index_) {
		return;
	}

	// Convert the absolute-index get_selected_row to a relative index for the create_engine to handle
	selected_game_index_ = convert_to_game_filtered_index(selected_game);

	create_engine_.set_current_level(selected_game_index_);

	sync_with_depcheck();

	update_details();

	// General settings
	const bool can_select_era = create_engine_.current_level().allow_era_choice();

	if(!can_select_era) {
		eras_menu_button_->set_label(_("No eras available for this game."));
	} else {
		eras_menu_button_->set_selected(eras_menu_button_->get_value());
	}

	eras_menu_button_->set_active(can_select_era);

	// Custom options
	options_manager_->update_game_options();

	// Game settings
	update_map_settings();
}

void mp_create_game::on_tab_select()
{
	const int i = find_widget<listbox>("tab_bar").get_selected_row();
	find_widget<stacked_widget>("pager").select_layer(i);
}

void mp_create_game::on_mod_toggle(const std::string& id, toggle_button* sender)
{
	if(sender && (sender->get_value_bool() == create_engine_.dependency_manager().is_modification_active(id))) {
		ERR_MP << "ignoring on_mod_toggle that is already set";
		return;
	}

	create_engine_.toggle_mod(id);

	sync_with_depcheck();

	options_manager_->update_mod_options();
}

void mp_create_game::on_era_select()
{
	create_engine_.set_current_era_index(eras_menu_button_->get_value());

	eras_menu_button_->set_tooltip(create_engine_.current_era().description);

	sync_with_depcheck();

	options_manager_->update_era_options();
}

void mp_create_game::on_random_faction_mode_select()
{
	selected_rfm_index_ = find_widget<menu_button>("random_faction_mode").get_value();
}

void mp_create_game::show_description(const std::string& new_description)
{
	styled_widget& description = find_widget<styled_widget>("description");

	description.set_label(!new_description.empty() ? new_description : _("No description available."));
	description.set_use_markup(true);
}

void mp_create_game::update_games_list()
{
	const int index = find_widget<menu_button>("game_types").get_value();

	display_games_of_type(level_types_[index].first, create_engine_.current_level().id());
	find_widget<button>("save_preset").set_active(level_types_[index].first == level_type::type::scenario);
}

void mp_create_game::display_games_of_type(level_type::type type, const std::string& level)
{
	create_engine_.set_current_level_type(type);

	listbox& list = find_widget<listbox>("games_list");

	list.clear();

	for(const auto& game : create_engine_.get_levels_by_type_unfiltered(type)) {
		widget_data data;
		widget_item item;

		if(type == level_type::type::campaign || type == level_type::type::sp_campaign) {
			item["label"] = game->icon();
			data.emplace("game_icon", item);
		}

		item["label"] = game->name();
		data.emplace("game_name", item);

		grid& rg = list.add_row(data);

		auto& icon = rg.find_widget<image>("game_icon");
		if(icon.get_label().empty()) {
			icon.set_visible(gui2::widget::visibility::invisible);
		}
	}

	if(!level.empty() && !list.get_rows_shown().empty()) {
		// Recalculate which rows should be visible
		on_filter_change<slider>("num_players", false);
		on_filter_change<text_box>("game_filter", false);

		int level_index = create_engine_.find_level_by_id(level).second;
		if(level_index >= 0 && std::size_t(level_index) < list.get_item_count()) {
			list.select_row(level_index);
		}
	}

	const bool is_random_map = type == level_type::type::random_map;

	find_widget<button>("random_map_regenerate").set_active(is_random_map);
	find_widget<button>("random_map_settings").set_active(is_random_map);

	// Override the last selection so on_game_select selects the new level
	selected_game_index_ = -1;

	on_game_select();
}

void mp_create_game::show_generator_settings()
{
	create_engine_.generator_user_config();

	regenerate_random_map();
}

void mp_create_game::regenerate_random_map()
{
	create_engine_.init_generated_level_data();

	update_details();
}

int mp_create_game::convert_to_game_filtered_index(const unsigned int initial_index)
{
	const std::vector<std::size_t>& filtered_indices = create_engine_.get_filtered_level_indices(create_engine_.current_level_type());
	return std::distance(filtered_indices.begin(), std::find(filtered_indices.begin(), filtered_indices.end(), initial_index));
}

void mp_create_game::update_details()
{
	styled_widget& players = find_widget<styled_widget>("map_num_players");
	styled_widget& map_size = find_widget<styled_widget>("map_size");

	if(create_engine_.current_level_type() == level_type::type::random_map) {
		// If the current random map doesn't have data, generate it
		if(create_engine_.generator_assigned() &&
			create_engine_.current_level().data()["map_data"].empty() &&
			create_engine_.current_level().data()["map_file"].empty()) {
			create_engine_.init_generated_level_data();
		}

		find_widget<button>("random_map_settings").set_active(create_engine_.generator_has_settings());
	}

	create_engine_.current_level().set_metadata();

	// Reset the mp_parameters with the defaults
	settings::set_default_values(create_engine_);

	// Set the title, with newlines replaced. Newlines are sometimes found in SP Campaign names
	std::string title = create_engine_.current_level().name();
	boost::replace_all(title, "\n", " " + font::unicode_em_dash + " ");
	find_widget<styled_widget>("game_title").set_label(title);


	switch(create_engine_.current_level_type()) {
		case level_type::type::preset:
		case level_type::type::scenario:
		case level_type::type::user_map:
		case level_type::type::user_scenario:
		case level_type::type::random_map: {
			ng::scenario* current_scenario = dynamic_cast<ng::scenario*>(&create_engine_.current_level());

			assert(current_scenario);

			create_engine_.get_state().classification().campaign = "";

			find_widget<stacked_widget>("minimap_stack").select_layer(0);

			if(current_scenario->data()["map_data"].empty()) {
				saved_game::expand_map_file(current_scenario->data());
				current_scenario->set_metadata();
			}

			find_widget<minimap>("minimap").set_map_data(current_scenario->data()["map_data"]);

			players.set_label(std::to_string(current_scenario->num_players()));
			map_size.set_label(current_scenario->map_size());

			break;
		}
		case level_type::type::campaign:
		case level_type::type::sp_campaign: {
			ng::campaign* current_campaign = dynamic_cast<ng::campaign*>(&create_engine_.current_level());

			assert(current_campaign);

			create_engine_.get_state().classification().campaign = current_campaign->data()["id"].str();

			const std::string img = formatter() << current_campaign->data()["image"] << "~SCALE_INTO(265,265)";

			find_widget<stacked_widget>("minimap_stack").select_layer(1);
			find_widget<image>("campaign_image").set_image(img);

			const int p_min = current_campaign->min_players();
			const int p_max = current_campaign->max_players();

			if(p_max > p_min) {
				players.set_label(VGETTEXT("number of players^$min to $max", {{"min", std::to_string(p_min)}, {"max", std::to_string(p_max)}}));
			} else {
				players.set_label(std::to_string(p_min));
			}

			map_size.set_label(font::unicode_em_dash);

			break;
		}
	}

	// This needs to be at the end, since errors found expanding the map data are put into the description
	show_description(create_engine_.current_level().description());
}

void mp_create_game::update_map_settings()
{
	config& level = create_engine_.current_level().data();
	if(level["force_lock_settings"].to_bool(!create_engine_.get_state().classification().is_normal_mp_game())) {
		use_map_settings_->widget_set_enabled(false, false);
		use_map_settings_->set_widget_value(true);
	} else {
		use_map_settings_->widget_set_enabled(true, false);
	}

	create_engine_.get_state().mp_settings().use_map_settings = use_map_settings_->get_widget_value();

	const bool use_map_settings = create_engine_.get_state().mp_settings().use_map_settings;

	fog_            ->widget_set_enabled(!use_map_settings, false);
	shroud_         ->widget_set_enabled(!use_map_settings, false);
	start_time_     ->widget_set_enabled(!use_map_settings, false);

	turns_          ->widget_set_enabled(!use_map_settings, false);
	gold_           ->widget_set_enabled(!use_map_settings, false);
	support_        ->widget_set_enabled(!use_map_settings, false);
	experience_     ->widget_set_enabled(!use_map_settings, false);

	const bool time_limit = time_limit_->get_widget_value();

	init_turn_limit_->widget_set_enabled(time_limit, false);
	turn_bonus_     ->widget_set_enabled(time_limit, false);
	reservoir_      ->widget_set_enabled(time_limit, false);
	action_bonus_   ->widget_set_enabled(time_limit, false);

	find_widget<button>("reset_timer_defaults").set_active(time_limit);

	fog_       ->set_widget_value(settings::fog_game_default(create_engine_));
	shroud_    ->set_widget_value(settings::shroud_game_default(create_engine_));
	start_time_->set_widget_value(settings::random_start_time_default(create_engine_));

	turns_     ->set_widget_value(settings::num_turns_default(create_engine_));
	gold_      ->set_widget_value(settings::village_gold_default(create_engine_));
	support_   ->set_widget_value(settings::village_support_default(create_engine_));
	experience_->set_widget_value(settings::xp_modifier_default(create_engine_));
}

void mp_create_game::load_game_callback()
{
	savegame::loadgame load(savegame::save_index_class::default_saves_dir(), create_engine_.get_state());

	if(!load.load_multiplayer_game()) {
		return;
	}

	if(load.data().cancel_orders) {
		create_engine_.get_state().cancel_orders();
	}

	set_retval(LOAD_GAME);
}

void mp_create_game::save_preset()
{
	config preset;
	preset["scenario"] = create_engine_.current_level().id();
	preset["era"] = create_engine_.current_era().id;
	preset["fog"] = fog_->get_widget_value();
	preset["shroud"] = shroud_->get_widget_value();
	preset["village_gold"] = gold_->get_widget_value();
	preset["village_support"] = support_->get_widget_value();
	preset["experience_modifier"] = experience_->get_widget_value();
	preset["countdown"] = time_limit_->get_widget_value();
	preset["countdown_turn_limit"] = init_turn_limit_->get_widget_value();
	preset["countdown_action_bonus"] = action_bonus_->get_widget_value();
	preset["countdown_turn_bonus"] = turn_bonus_->get_widget_value();
	preset["countdown_reservoir"] = reservoir_->get_widget_value();
	preset["random_start_time"] = start_time_->get_widget_value();
	preset["shuffle_sides"] = shuffle_sides_->get_widget_value();
	preset["turns"] = turns_->get_widget_value();
	preset["observer"] = observers_->get_widget_value();
	preset["use_map_settings"] = use_map_settings_->get_widget_value();

	prefs::get().add_game_preset(std::move(preset));
}

std::vector<std::string> mp_create_game::get_active_mods()
{
	int i = 0;
	std::set<std::string> res;
	for(const auto& mod : create_engine_.get_extras_by_type(ng::create_engine::MOD)) {
		if(mod_list_->get_row_grid(i)->find_widget<toggle_button>("mod_active_state").get_value_bool()) {
			res.insert(mod->id);
		}
		++i;
	}
	return std::vector<std::string>(res.begin(), res.end());
}

void mp_create_game::set_active_mods(const std::vector<std::string>& val)
{
	std::set<std::string> val2(val.begin(), val.end());
	int i = 0;
	std::set<std::string> res;
	for(const auto& mod : create_engine_.get_extras_by_type(ng::create_engine::MOD)) {
		mod_list_->get_row_grid(i)->find_widget<toggle_button>("mod_active_state").set_value_bool(val2.find(mod->id) != val2.end());
		++i;
	}
}

void mp_create_game::reset_timer_settings()
{
	// This allows the defaults to be returned by the pref getters below
	prefs::get().clear_countdown_init_time();
	prefs::get().clear_countdown_reservoir_time();
	prefs::get().clear_countdown_turn_bonus();
	prefs::get().clear_countdown_action_bonus();

	init_turn_limit_->set_widget_value(prefs::get().countdown_init_time().count());
	turn_bonus_->set_widget_value(prefs::get().countdown_turn_bonus().count());
	reservoir_->set_widget_value(prefs::get().countdown_reservoir_time().count());
	action_bonus_->set_widget_value(prefs::get().countdown_action_bonus().count());
}

bool mp_create_game::dialog_exit_hook()
{
	if(!create_engine_.current_level_has_side_data()) {
		gui2::show_transient_error_message(_("The selected game has no sides!"));
		return false;
	}

	if(!create_engine_.current_level().can_launch_game()) {
		std::stringstream msg;
		// TRANSLATORS: This sentence will be followed by some details of the error, most likely the "Map could not be loaded" message from create_engine.cpp
		msg << _("The selected game cannot be created.");
		msg << "\n\n";
		msg << create_engine_.current_level().description();
		gui2::show_transient_error_message(msg.str());
		return false;
	}

	if(!create_engine_.is_campaign()) {
		return true;
	}

	return create_engine_.select_campaign_difficulty() != "CANCEL";
}

void mp_create_game::post_show()
{
	plugins_context_.reset();

	// Show all tabs so that find_widget works correctly
	find_widget<stacked_widget>("pager").select_layer(-1);

	if(get_retval() == LOAD_GAME) {
		create_engine_.prepare_for_saved_game();

		// We don't need the LOAD_GAME retval past this point. For convenience, reset it to OK so we can use the execute wrapper, then exit.
		set_retval(retval::OK);
		return;
	}

	if(get_retval() == retval::OK) {
		prefs::get().set_modifications(create_engine_.active_mods());
		prefs::get().set_mp_level_type(static_cast<int>(create_engine_.current_level_type()));
		prefs::get().set_mp_level(create_engine_.current_level().id());
		prefs::get().set_mp_era(create_engine_.current_era().id);

		create_engine_.prepare_for_era_and_mods();

		if(create_engine_.current_level_type() == level_type::type::campaign ||
			create_engine_.current_level_type() == level_type::type::sp_campaign) {
			create_engine_.prepare_for_campaign();
		} else if(create_engine_.current_level_type() == level_type::type::scenario) {
			create_engine_.prepare_for_scenario();
		} else {
			// This means define= doesn't work for randomly generated scenarios
			create_engine_.prepare_for_other();
		}

		create_engine_.get_parameters();

		create_engine_.prepare_for_new_level();

		std::vector<const config*> entry_points;
		std::vector<std::string> entry_point_titles;

		const auto& tagname = create_engine_.get_state().classification().get_tagname();

		if(tagname == "scenario") {
			const std::string first_scenario = create_engine_.current_level().data()["first_scenario"];
			for(const config& scenario : game_config_manager::get()->game_config().child_range(tagname)) {
				const bool is_first = scenario["id"] == first_scenario;
				if(scenario["allow_new_game"].to_bool(false) || is_first || game_config::debug ) {
					const std::string& title = !scenario["new_game_title"].empty()
						? scenario["new_game_title"]
						: scenario["name"];

					entry_points.insert(is_first ? entry_points.begin() : entry_points.end(), &scenario);
					entry_point_titles.insert(is_first ? entry_point_titles.begin() : entry_point_titles.end(), title);
				}
			}
		}

		mp_game_settings& params = create_engine_.get_state().mp_settings();
		if(entry_points.size() > 1) {
			gui2::dialogs::simple_item_selector dlg(_("Choose Starting Scenario"), _("Select at which point to begin this campaign."), entry_point_titles);

			dlg.set_single_button(true);
			dlg.show();

			const config& scenario = *entry_points[dlg.selected_index()];

			params.hash = scenario.hash();
			create_engine_.get_state().set_scenario(scenario);
		}

		params.use_map_settings = use_map_settings_->get_widget_value();

		if(!create_engine_.current_level().data()["force_lock_settings"].to_bool(!create_engine_.get_state().classification().is_normal_mp_game())) {
			// Max slider value (in this case, 100) means 'unlimited turns', so pass the value -1
			const int num_turns = turns_->get_widget_value();
			params.num_turns = num_turns < ::settings::turns_max ? num_turns : - 1;
			params.village_gold = gold_->get_widget_value();
			params.village_support = support_->get_widget_value();
			params.xp_modifier = experience_->get_widget_value();
			params.random_start_time = start_time_->get_widget_value();
			params.fog_game = fog_->get_widget_value();
			params.shroud_game = shroud_->get_widget_value();

			// write to scenario
			config& scenario = create_engine_.get_state().get_starting_point();

			if(params.random_start_time) {
				if(!tod_manager::is_start_ToD(scenario["random_start_time"])) {
					scenario["random_start_time"] = true;
				}
			} else {
				scenario["random_start_time"] = false;
			}

			scenario["experience_modifier"] = params.xp_modifier;
			scenario["turns"] = params.num_turns;

			for(config& side : scenario.child_range("side")) {
				if(!params.use_map_settings) {
					side["fog"] = params.fog_game;
					side["shroud"] = params.shroud_game;
					side["village_gold"] = params.village_gold;
					side["village_support"] = params.village_support;
				} else {
					if(side["fog"].empty()) {
						side["fog"] = params.fog_game;
					}

					if(side["shroud"].empty()) {
						side["shroud"] = params.shroud_game;
					}

					if(side["village_gold"].empty()) {
						side["village_gold"] = params.village_gold;
					}

					if(side["village_support"].empty()) {
						side["village_support"] = params.village_support;
					}
				}
			}
		}

		params.mp_countdown = time_limit_->get_widget_value();
		params.mp_countdown_init_time = std::chrono::seconds{init_turn_limit_->get_widget_value()};
		params.mp_countdown_turn_bonus = std::chrono::seconds{turn_bonus_->get_widget_value()};
		params.mp_countdown_reservoir_time = std::chrono::seconds{reservoir_->get_widget_value()};
		params.mp_countdown_action_bonus = std::chrono::seconds{action_bonus_->get_widget_value()};

		params.allow_observers = observers_->get_widget_value();
		params.private_replay = private_replay_->get_widget_value();
		create_engine_.get_state().classification().oos_debug = strict_sync_->get_widget_value();
		params.shuffle_sides = shuffle_sides_->get_widget_value();

		random_faction_mode::type type = random_faction_mode::get_enum(selected_rfm_index_).value_or(random_faction_mode::type::independent);
		params.mode = type;

		// Since we don't have a field handling this option, we need to save the value manually
		prefs::get().set_random_faction_mode(random_faction_mode::get_string(type));

		// Save custom option settings
		params.options = options_manager_->get_options_config();
		prefs::get().set_options(options_manager_->get_options_config());

		// Set game name
		const std::string name = find_widget<text_box>("game_name").get_value();
		if(!name.empty() && (name != settings::game_name_default())) {
			params.name = name;
		}

		// Set game password
		const std::string password = find_widget<text_box>("game_password").get_value();
		if(!password.empty()) {
			params.password = password;
		}
	}
}

} // namespace dialogs
