#include "game/detail/view_input/sound_effect_input.h"
#include "game/transcendental/entity_handle.h"
#include "game/transcendental/logic_step.h"
#include "game/messages/start_sound_effect.h"
#include "game/transcendental/data_living_one_step.h"
#include "game/transcendental/cosmos.h"

void sound_effect_input::start(
	const logic_step step, 
	const sound_effect_start_input start
) const {
	messages::start_sound_effect msg;
	msg.effect = *this;
	msg.start = start;

	if (msg.start.variation_number == static_cast<std::size_t>(-1)) {
		if (const auto c = std::get_if<orbital_chasing>(&start.positioning)) {
			if (const auto subject = step.get_cosmos()[c->target]) {
				msg.start.set_variation_from(subject);
			}
		}
		else {
			msg.start.set_variation_from(std::get<components::transform>(start.positioning));
		}
	}

	step.post_message(msg);
}

sound_effect_start_input& sound_effect_start_input::set_variation_from(const const_entity_handle h) {
	variation_number = augs::simple_two_hash(h.get_guid(), h.get_logic_transform().pos);
	return *this;
}

sound_effect_start_input sound_effect_start_input::orbit_absolute(const const_entity_handle h, components::transform world_transform) {
	return orbit_local(h.get_id(), augs::get_relative_offset(h.get_logic_transform(), world_transform));
}
