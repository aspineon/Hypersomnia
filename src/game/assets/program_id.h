#pragma once
#include "game/container_sizes.h"
#include "game/build_settings.h"

namespace assets {
	enum class program_id {
		INVALID,
#if BUILD_UNSCRIPTED_TEST_SCENES
		DEFAULT,
		DEFAULT_ILLUMINATED,
		LIGHT,
		PURE_COLOR_HIGHLIGHT,
		CIRCULAR_BARS,
		EXPLODING_RING,
		SMOKE,
		ILLUMINATING_SMOKE,
		SPECULAR_HIGHLIGHTS,
		FULLSCREEN,
#endif
		COUNT = MAX_SHADER_PROGRAM_COUNT + 1
	};
}