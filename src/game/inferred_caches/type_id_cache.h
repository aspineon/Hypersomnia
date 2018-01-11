#pragma once
#include <unordered_set>
#include <unordered_map>

#include "game/transcendental/entity_id.h"
#include "game/transcendental/entity_handle_declaration.h"

#include "game/transcendental/entity_type_declaration.h"

namespace components {
	struct type;
}

class type_id_cache {
	std::unordered_map<
		entity_type_id,
		std::unordered_set<entity_id>
	> entities_by_type_id;

	void infer_cache_for(const entity_id, const components::type&);
	void destroy_cache_of(const entity_id, const components::type&);

public:
	std::unordered_set<entity_id> get_entities_by_type_id(const entity_type_id) const;

	void infer_cache_for(const const_entity_handle);
	void destroy_cache_of(const const_entity_handle);
};