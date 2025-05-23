/*
	Copyright (C) 2016 - 2025
	by Jyrki Vesterinen <sandgtx@gmail.com>
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

#include "gui/widgets/size_lock.hpp"

#include "gettext.hpp"
#include "gui/core/layout_exception.hpp"
#include "gui/core/register_widget.hpp"
#include "gui/widgets/helper.hpp"
#include "wml_exception.hpp"

namespace gui2
{
REGISTER_WIDGET(size_lock)

size_lock::size_lock(const implementation::builder_size_lock& builder)
	: container_base(builder, type())
	, width_(builder.width_)
	, height_(builder.height_)
	, widget_(nullptr)
{
}

void size_lock::place(const point& origin, const point& size)
{
	point content_size = widget_->get_best_size();

	if(content_size.x > size.x) {
		reduce_width(size.x);
		content_size = widget_->get_best_size();
	}

	if(content_size.y > size.y) {
		try {
			reduce_height(size.y);
		} catch(const layout_exception_width_modified&) {
		}

		content_size = widget_->get_best_size();
	}

	if(content_size.x > size.x) {
		reduce_width(size.x);
		content_size = widget_->get_best_size();
	}

	container_base::place(origin, size);
}

void size_lock::layout_children()
{
	assert(widget_ != nullptr);

	widget_->layout_children();
}

void size_lock::finalize(const builder_widget& widget_builder)
{
	set_rows_cols(1u, 1u);

	auto widget = widget_builder.build();
	widget_ = widget.get();

	set_child(std::move(widget), 0u, 0u, grid::VERTICAL_GROW_SEND_TO_CLIENT | grid::HORIZONTAL_GROW_SEND_TO_CLIENT, 0u);
}

point size_lock::calculate_best_size() const
{
	const wfl::map_formula_callable& size = get_screen_size_variables();

	unsigned width = width_(size);
	unsigned height = height_(size);

	VALIDATE(width > 0 || height > 0, _("Invalid size."));

	return point(width, height);
}

size_lock_definition::size_lock_definition(const config& cfg)
	: styled_widget_definition(cfg)
{
	DBG_GUI_P << "Parsing fixed size widget " << id;

	load_resolutions<resolution>(cfg);
}

size_lock_definition::resolution::resolution(const config& cfg)
	: resolution_definition(cfg)
	, grid(nullptr)
{
	// Add a dummy state since every widget needs a state.
	static config dummy("draw");
	state.emplace_back(dummy);

	auto child = cfg.optional_child("grid");
	VALIDATE(child, _("No grid defined."));

	grid = std::make_shared<builder_grid>(*child);
}

namespace implementation
{
builder_size_lock::builder_size_lock(const config& cfg)
	: builder_styled_widget(cfg)
	, width_(cfg["width"])
	, height_(cfg["height"])
	, content_(nullptr)
{
	VALIDATE(cfg.has_child("widget"), _("No widget defined."));
	content_ = create_widget_builder(cfg.mandatory_child("widget"));
}

std::unique_ptr<widget> builder_size_lock::build() const
{
	auto widget = std::make_unique<size_lock>(*this);

	DBG_GUI_G << "Window builder: placed fixed size widget '" << id << "' with definition '" << definition << "'.";

	const auto conf = widget->cast_config_to<size_lock_definition>();
	assert(conf != nullptr);

	widget->init_grid(*conf->grid);
	widget->finalize(*content_);

	return widget;
}
}
}
