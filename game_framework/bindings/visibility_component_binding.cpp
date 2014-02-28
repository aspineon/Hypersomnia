#pragma once
#include "stdafx.h"
#include "bindings.h"

#include "../components/visibility_component.h"
#include "../systems/visibility_system.h"

namespace bindings {
	luabind::scope _visibility_component() {
		return
			luabind::class_<visibility_system>("_visibility_system")
			.def_readwrite("draw_cast_rays", &visibility_system::draw_cast_rays)
			.def_readwrite("draw_triangle_edges", &visibility_system::draw_triangle_edges)
			.def_readwrite("draw_discontinuities", &visibility_system::draw_discontinuities)
			.def_readwrite("draw_visible_walls", &visibility_system::draw_visible_walls)
			.def_readwrite("epsilon_ray_distance_variation", &visibility_system::epsilon_ray_distance_variation)
			.def_readwrite("epsilon_distance_vertex_hit", &visibility_system::epsilon_distance_vertex_hit)
			.def_readwrite("epsilon_threshold_obstacle_hit", &visibility_system::epsilon_threshold_obstacle_hit)
			,

			luabind::class_<visibility::edge>("visibility_edge")
			.def_readwrite("first", &visibility::edge::first)
			.def_readwrite("second", &visibility::edge::second),

			luabind::class_<visibility::discontinuity>("visibility_discontinuity")
			.def(luabind::constructor<>())
			.def_readwrite("points", &visibility::discontinuity::points)
			.def_readwrite("winding", &visibility::discontinuity::winding),
			

			luabind::class_<visibility::layer>("visibility_layer")
			.def(luabind::constructor<>())
			.def_readwrite("filter", &visibility::layer::filter)
			.def_readwrite("square_side", &visibility::layer::square_side)
			.def_readwrite("color", &visibility::layer::color)
			.def_readwrite("ignore_discontinuities_shorter_than", &visibility::layer::ignore_discontinuities_shorter_than)
			.def_readwrite("offset", &visibility::layer::offset)
			.def_readwrite("postprocessing_subject", &visibility::layer::postprocessing_subject)
			.def("get_polygon", &visibility::layer::get_polygon)
			.def("get_discontinuity", &visibility::layer::get_discontinuity)
			.def("get_num_discontinuities", &visibility::layer::get_num_discontinuities)
			, 
			
			luabind::class_<visibility>("visibility_component")
			.def(luabind::constructor<>())
			.def("add_layer", &visibility::add_layer)
			.def("get_layer", &visibility::get_layer)
			.enum_("constants")
			[
				luabind::value("OBSTACLE_AVOIDANCE", visibility::OBSTACLE_AVOIDANCE),
				luabind::value("CONTAINMENT", visibility::CONTAINMENT),
				luabind::value("DYNAMIC_PATHFINDING", visibility::DYNAMIC_PATHFINDING)
			]
			;
	}
}