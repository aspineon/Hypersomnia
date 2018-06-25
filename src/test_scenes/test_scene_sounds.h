#pragma once
#include "test_scenes/test_id_to_pool_id.h"

enum class test_scene_sound_id {
	// GEN INTROSPECTOR enum class test_scene_sound_id
	BILMER2000_MUZZLE,
	ASSAULT_RIFLE_MUZZLE,
	VINDICATOR_MUZZLE,
	KEK9_MUZZLE,
	PLASMA_MUZZLE,
	ELECTRIC_PROJECTILE_FLIGHT,
	ELECTRIC_DISCHARGE_EXPLOSION,
	MISSILE_THRUSTER,

	IMPACT,
	DEATH,
	BULLET_PASSES_THROUGH_HELD_ITEM,

	WIND,
	ENGINE,

	LOW_AMMO_CUE,
#if 0
	VINDICATOR_LOW_AMMO_CUE,
#endif

	FIREARM_ENGINE,
#if 0
	FIREARM_HEAT_ENGINE,
#endif

	HEAVY_HEAT_START,
	LIGHT_HEAT_START,

	CAST_SUCCESSFUL,
	CAST_UNSUCCESSFUL,

	CAST_CHARGING,

	EXPLOSION,
	GREAT_EXPLOSION,

	INTERFERENCE_EXPLOSION,
	PED_EXPLOSION,

	GRENADE_UNPIN,
	GRENADE_THROW,

	ITEM_THROW,

	COLLISION_METAL_WOOD,
	COLLISION_METAL_METAL,
	COLLISION_GRENADE,
	STANDARD_FOOTSTEP,
	FOOTSTEP_DIRT,
	FOOTSTEP_FLOOR,

	STANDARD_HOLSTER,

	STANDARD_GUN_DRAW,
	STANDARD_PISTOL_DRAW,
	LEWSII_DRAW,

	BACKPACK_WEAR,
	BACKPACK_INSERT,

	STEAM_BURST,

	PLASMA_DRAW,

	GLASS_DAMAGE,
	COLLISION_GLASS,

	STEEL_PROJECTILE_DESTRUCTION,

	COUNT
	// END GEN INTROSPECTOR
};

inline auto to_sound_id(const test_scene_sound_id id) {
	return to_pool_id<assets::sound_id>(id);
}
