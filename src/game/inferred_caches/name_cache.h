#pragma once
#include <unordered_set>
#include <unordered_map>

#include "augs/misc/children_vector_tracker.h"
#include "game/transcendental/entity_id.h"
#include "game/transcendental/entity_handle_declaration.h"
#include "game/components/type_component_declaration.h"

namespace components {
	struct type;
}

class entity_types;

template <bool, class>
class component_synchronizer;

class name_cache {
	std::unordered_map<
		entity_type_id,
		std::unordered_set<entity_id>
	> entities_by_type_id;

	friend class cosmos;
	friend class cosmos_solvable_state;
	friend class component_synchronizer<false, components::type>;

	void reserve_caches_for_entities(const std::size_t n) const {}

	void infer_cache_for(const const_entity_handle);
	void destroy_cache_of(const const_entity_handle);

	void infer_cache_for(const entity_id, const components::type&);
	void destroy_cache_of(const entity_id, const components::type&);

public:
	std::unordered_set<entity_id> get_entities_by_type_id(const entity_type_id) const;

};