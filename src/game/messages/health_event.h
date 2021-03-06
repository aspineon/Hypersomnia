#pragma once
#include "game/messages/message.h"
#include "game/detail/damage_origin.h"

#include "augs/misc/value_meter.h"

namespace messages {
	struct health_event : message {
		enum class result_type {
			NONE,
			PERSONAL_ELECTRICITY_DESTRUCTION,
			DEATH,
			LOSS_OF_CONSCIOUSNESS
		} special_result = result_type::NONE;

		enum class target_type {
			INVALID,
			PERSONAL_ELECTRICITY,
			CONSCIOUSNESS,
			HEALTH
		} target = target_type::INVALID;

		vec2 point_of_impact;
		vec2 impact_velocity;

		damage_origin origin;

		float ratio_effective_to_maximum = 1.f;
		meter_value_type effective_amount = 0;

		bool was_conscious = true;

		static auto request_death(
			const entity_id of_whom,
			const vec2 direction,
			const vec2 point_of_impact,
			const damage_origin& origin
		) {
			health_event output;
			output.subject = of_whom;
			output.point_of_impact = point_of_impact;
			output.impact_velocity = direction;
			output.effective_amount = 0;
			output.special_result = result_type::DEATH;
			output.origin = origin;
			output.target = target_type::HEALTH;
			return output;
		}
	};
}