#pragma once
#include "test_scenes/test_id_to_pool_id.h"
#include "test_scenes/test_scene_images.h"

enum class test_scene_plain_animation_id {
	// GEN INTROSPECTOR enum class test_scene_plain_animation_id
	CAST_BLINK_ANIMATION,
	VINDICATOR_SHOOT,
	COUNT
	// END GEN INTROSPECTOR
};

enum class test_scene_torso_animation_id {
	// GEN INTROSPECTOR enum class test_scene_torso_animation_id
	METROPOLIS_CHARACTER_BARE,
	RESISTANCE_CHARACTER_BARE,

	METROPOLIS_CHARACTER_AKIMBO,

	RESISTANCE_CHARACTER_RIFLE,
	RESISTANCE_CHARACTER_RIFLE_SHOOT,

	COUNT
	// END GEN INTROSPECTOR
};

enum class test_scene_legs_animation_id {
	// GEN INTROSPECTOR enum class test_scene_legs_animation_id
	SILVER_TROUSERS,
	SILVER_TROUSERS_STRAFE,
	COUNT
	// END GEN INTROSPECTOR
};

inline auto to_animation_id(const test_scene_plain_animation_id id) {
	return to_pool_id<assets::plain_animation_id>(id);
}

inline auto to_animation_id(const test_scene_torso_animation_id id) {
	return to_pool_id<assets::torso_animation_id>(id);
}

inline auto to_animation_id(const test_scene_legs_animation_id id) {
	return to_pool_id<assets::legs_animation_id>(id);
}
