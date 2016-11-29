#include "tile_layer_instance_component.h"

#include "sprite_component.h"

#include "game/detail/state_for_drawing_camera.h"

#include "augs/graphics/vertex.h"
#include "augs/ensure.h"
#include "game/resources/manager.h"

using namespace components;
using namespace augs;

namespace components {
	tile_layer_instance::tile_layer_instance(const assets::tile_layer_id id) : id(id) {}

	ltrbu tile_layer_instance::get_visible_tiles(const drawing_input & in) const {
		ltrb visible_tiles;
		const auto visible_aabb = in.camera.get_transformed_visible_world_area_aabb();
		const auto& layer = (*id);
		const float tile_square_size = layer.get_tile_side();

		visible_tiles.l = int((visible_aabb.l - in.renderable_transform.pos.x) / tile_square_size);
		visible_tiles.t = int((visible_aabb.t - in.renderable_transform.pos.y) / tile_square_size);
		visible_tiles.r = int((visible_aabb.r - in.renderable_transform.pos.x) / tile_square_size) + 1;
		visible_tiles.b = int((visible_aabb.b - in.renderable_transform.pos.y) / tile_square_size) + 1;
		visible_tiles.l = std::max(0.f, visible_tiles.l);
		visible_tiles.t = std::max(0.f, visible_tiles.t);
		visible_tiles.r = std::min(float(layer.get_size().x), visible_tiles.r);
		visible_tiles.b = std::min(float(layer.get_size().y), visible_tiles.b);

		return ltrbu(visible_tiles.l, visible_tiles.t, visible_tiles.r, visible_tiles.b);
	}

	void tile_layer_instance::draw(const drawing_input & in) const {
		/* if it is not visible, return */
		const auto visible_aabb = in.camera.get_transformed_visible_world_area_aabb();
		const auto& layer = (*id);
		const float tile_square_size = layer.get_tile_side();
		const auto size = layer.get_size();

		if (!visible_aabb.hover(xywh(
			in.renderable_transform.pos.x, 
			in.renderable_transform.pos.y, 
			size.x*tile_square_size, 
			size.y*tile_square_size))) {
			return;
		}
		
		auto visible_tiles = get_visible_tiles(in);
		
		sprite::drawing_input sprite_input(in.target_buffer);
		sprite_input.camera = in.camera;
		sprite_input.colorize = in.colorize;
		sprite_input.use_neon_map = in.use_neon_map;

		sprite tile_sprite;

		for (unsigned y = visible_tiles.t; y < visible_tiles.b; ++y) {
			for (unsigned x = visible_tiles.l; x < visible_tiles.r; ++x) {
				vertex_triangle t1, t2;
		
				const auto& tile = layer.tile_at({ x, y });
				if (tile.type_id == 0) continue;

				auto tile_offset = vec2i(x, y) * tile_square_size;

				const auto& type = layer.get_tile_type(tile);
		
				tile_sprite.tex = type.tile_texture;
		
				sprite_input.renderable_transform.pos = vec2i(in.renderable_transform.pos) + tile_offset + vec2(tile_square_size / 2, tile_square_size / 2);
		
				tile_sprite.draw(sprite_input);
			}
		}
	}


	rects::ltrb<float> tile_layer_instance::get_aabb(const components::transform transform) const {
		const auto& layer = (*id);
		const float tile_square_size = layer.get_tile_side();
		const auto size = layer.get_size();

		return rects::xywh<float>(transform.pos.x, transform.pos.y, static_cast<float>(size.x*tile_square_size), static_cast<float>(size.y*tile_square_size));
	}
}
