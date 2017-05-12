#pragma once
#include "game/container_sizes.h"
#include "game/build_settings.h"
#include "game/assets/particle_effect.h"

namespace assets {
	enum class physical_material_id {
		INVALID,
#if BUILD_UNSCRIPTED_TEST_SCENES
		WOOD,
		METAL,
		GRENADE,
#endif
		COUNT = MAX_PHYSICAL_MATERIAL_COUNT + 1
	};
}