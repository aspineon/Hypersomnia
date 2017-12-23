#include "game/organization/all_messages_includes.h"
#include "game/organization/all_component_includes.h"

#include "game/transcendental/standard_solver.h"
#include "game/transcendental/cosmos.h"
#include "game/transcendental/entity_handle.h"
#include "game/transcendental/data_living_one_step.h"

#include "game/detail/inventory/inventory_utils.h"
#include "game/detail/physics/contact_listener.h"

#include "game/stateless_systems/movement_system.h"
#include "game/stateless_systems/visibility_system.h"
#include "game/stateless_systems/pathfinding_system.h"
#include "game/stateless_systems/animation_system.h"
#include "game/stateless_systems/input_system.h"
#include "game/stateless_systems/gun_system.h"
#include "game/stateless_systems/crosshair_system.h"
#include "game/stateless_systems/rotation_copying_system.h"
#include "game/stateless_systems/position_copying_system.h"
#include "game/stateless_systems/missile_system.h"
#include "game/stateless_systems/destroy_system.h"
#include "game/stateless_systems/particles_existence_system.h"
#include "game/stateless_systems/behaviour_tree_system.h"
#include "game/stateless_systems/car_system.h"
#include "game/stateless_systems/driver_system.h"
#include "game/stateless_systems/item_system.h"
#include "game/stateless_systems/force_joint_system.h"
#include "game/stateless_systems/intent_contextualization_system.h"
#include "game/stateless_systems/trace_system.h"
#include "game/stateless_systems/melee_system.h"
#include "game/stateless_systems/sentience_system.h"
#include "game/stateless_systems/destruction_system.h"
#include "game/stateless_systems/sound_existence_system.h"
#include "game/stateless_systems/hand_fuse_system.h"
#include "game/stateless_systems/physics_system.h"

void standard_solve(const logic_step step) {
	auto& cosmos = step.get_cosmos();
	const auto delta = step.get_delta();
	auto& performance = cosmos.profiler;

	auto logic_scope = measure_scope(performance.logic);

	contact_listener listener(cosmos);

	perform_transfers(step.get_entropy().transfer_requests, step);

	performance.entropy_length.measure(step.get_entropy().length());

	sentience_system().cast_spells(step);

	input_system().make_input_messages(step);

	intent_contextualization_system().contextualize_crosshair_action_intents(step);
	intent_contextualization_system().contextualize_use_button_intents(step);
	intent_contextualization_system().contextualize_movement_intents(step);

	movement_system().set_movement_flags_from_input(step);
	movement_system().apply_movement_forces(cosmos);

	gun_system().consume_gun_intents(step);
	gun_system().launch_shots_due_to_pressed_triggers(step);

	car_system().set_steering_flags_from_intents(step);
	car_system().apply_movement_forces(step);

	melee_system().consume_melee_intents(step);
	melee_system().initiate_and_update_moves(step);

	force_joint_system().apply_forces_towards_target_entities(step);
	item_system().handle_throw_item_intents(step);
	hand_fuse_system().detonate_fuses(step);

	{
		auto scope = measure_scope(performance.physics);

		listener.during_step = true;
		physics_system().step_and_set_new_transforms(step);
		listener.during_step = false;
	}

	rotation_copying_system().update_rotations(cosmos);
	position_copying_system().update_transforms(step);

	trace_system().lengthen_sprites_of_traces(step);

	crosshair_system().generate_crosshair_intents(step);
	crosshair_system().apply_crosshair_intents_to_base_offsets(step);
	crosshair_system().apply_base_offsets_to_crosshair_transforms(step);

	//	item_system().translate_gui_intents_to_transfer_requests(step);
	item_system().start_picking_up_items(step);
	item_system().pick_up_touching_items(step);

	missile_system().detonate_colliding_missiles(step);
	missile_system().detonate_expired_missiles(step);

	destruction_system().generate_damages_from_forceful_collisions(step);
	destruction_system().apply_damages_and_split_fixtures(step);

	sentience_system().regenerate_values_and_advance_spell_logic(step);
	sentience_system().apply_damage_and_generate_health_events(step);
	physics_system().post_and_clear_accumulated_collision_messages(step);
	sentience_system().cooldown_aimpunches(step);

	driver_system().release_drivers_due_to_requests(step);
	driver_system().assign_drivers_who_touch_wheels(step);
	driver_system().release_drivers_due_to_ending_contact_with_wheel(step);

	particles_existence_system().game_responses_to_particle_effects(step);

	sound_existence_system().create_sounds_from_game_events(step);
	// gui_system().translate_game_events_for_hud(step);

	{
		auto scope = measure_scope(performance.visibility);
		visibility_system().respond_to_visibility_information_requests(step);
	}

	{
		auto scope = measure_scope(performance.ai);
		behaviour_tree_system().evaluate_trees(step);
	}

	{
		auto scope = measure_scope(performance.pathfinding);
		pathfinding_system().advance_pathfinding_sessions(step);
	}

	auto& transfers = step.get_queue<item_slot_transfer_request>();
	perform_transfers(transfers, step);

	particles_existence_system().displace_streams_and_destroy_dead_streams(step);
	sound_existence_system().destroy_dead_sounds(step);

	trace_system().destroy_outdated_traces(step);

	const size_t queued_before_marking_num = step.get_queue<messages::queue_destruction>().size();

	destroy_system().mark_queued_entities_and_their_children_for_deletion(step);

	trace_system().spawn_finishing_traces_for_deleted_entities(step);

	listener.~contact_listener();

	movement_system().generate_movement_events(step);

	animation_system().game_responses_to_animation_messages(step);

	animation_system().handle_animation_messages(step);
	animation_system().progress_animation_states(step);

	//position_copying_system().update_transforms(step);
	//rotation_copying_system().update_rotations(cosmos);

	performance.raycasts.measure(cosmos.solvable.inferred.physics.ray_casts_since_last_step);

	cosmos.solvable.increment_step();

	const size_t queued_at_end_num = step.get_queue<messages::queue_destruction>().size();

	ensure_eq(queued_at_end_num, queued_before_marking_num);
}