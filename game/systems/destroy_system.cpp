#include <functional>

#include "destroy_system.h"
#include "game/cosmos.h"
#include "game/entity_id.h"

#include "game/messages/queue_destruction.h"
#include "game/messages/will_soon_be_deleted.h"

#include "game/entity_handle.h"
#include "game/step_state.h"

#include "ensure.h"


void destroy_system::queue_children_of_queued_entities(cosmos& cosmos, step_state& step) {
	auto& queued = step.messages.get_queue<messages::queue_destruction>();
	auto& deletions = step.messages.get_queue<messages::will_soon_be_deleted>();

	for (auto it : queued) {
		auto deletion_adder = [&deletions](entity_id descendant) {
			deletions.push_back(descendant);
		};

		deletions.push_back(it.subject);
		cosmos[it.subject].for_each_sub_entity_recursive(deletion_adder);
	}

	queued.clear();
}

void destroy_system::perform_deletions(cosmos& cosmos, step_state& step) {
	auto& deletions = step.messages.get_queue<messages::will_soon_be_deleted>();

	// destroy in reverse order; children first
	for (auto& it = deletions.rbegin(); it != deletions.rend(); ++it) {
		ensure(cosmos.get_handle((*it).subject).alive());

		cosmos.delete_entity((*it).subject);
	}

	deletions.clear();
}
