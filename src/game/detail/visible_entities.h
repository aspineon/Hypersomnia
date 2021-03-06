#pragma once
#include "augs/misc/enum/enum_array.h"

#include "augs/templates/maybe.h"
#include "augs/math/camera_cone.h"

#include "game/enums/render_layer.h"
#include "game/cosmos/entity_id.h"

#include "game/detail/render_layer_filter.h"
#include "game/detail/tree_of_npo_filter.h"
#include "augs/enums/callback_result.h"

#include "augs/enums/accuracy_type.h"

struct visible_entities_query {
	const cosmos& cosm;
	const camera_cone cone;
	const accuracy_type accuracy;
	const augs::maybe<render_layer_filter> filter;
	const tree_of_npo_filter types;

	static auto dont_filter() {
		return augs::maybe<render_layer_filter>();
	}
};

template <class T>
using per_render_layer_t = augs::enum_array<T, render_layer>;

class visible_entities {
	using id_type = entity_id;
	
	using per_layer_type = per_render_layer_t<std::vector<id_type>>;
	per_layer_type per_layer;

	void register_visible(const cosmos&, entity_id);
	void sort_car_interiors(const cosmos&);

public:
	visible_entities() = default;

	visible_entities(const visible_entities_query);
	visible_entities& operator=(const visible_entities&) = delete;

	/*
		This function will be used instead of copy-assignment operator,
		in order to take advantage of the reserved space in containers.
	*/

	visible_entities& reacquire_all_and_sort(const visible_entities_query);
	
	void acquire_physical(const visible_entities_query);
	void acquire_non_physical(const visible_entities_query);
	
	void clear_dead_entities(const cosmos&);
	void clear();

	template <class F, class O>
	auto for_all_ids_ordered(F&& callback, const O& order) const {
		for (const auto& layer : order) {
			for (const auto id : per_layer[layer]) {
				callback(id);
			}
		}
	}

	auto count_all() const {
		return ::accumulate_sizes(per_layer);
	}

	template <class C, class F>
	void for_all(C& cosm, F&& callback) const {
		for (const auto& layer : per_layer) {
			for (const auto id : layer) {
				callback(cosm[id]);
			}
		}
	}

	template <render_layer... Args, class C, class F>
	void for_each(C& cosm, F&& callback) const {
		bool broken = false;

		auto looper = [&](const render_layer l) {
			if (broken) {
				return;
			}

			for (const auto& e : per_layer[l]) {
				if constexpr(std::is_same_v<callback_result, decltype(callback(cosm[e]))>) {
					if (callback_result::ABORT == callback(cosm[e])) {
						broken = true;
						break;
					}
				}
				else {
					callback(cosm[e]);
				}
			}
		};

		(looper(Args), ...);
	}


	template <class F>
	entity_id get_first_fulfilling(F condition) const;
};

inline auto& thread_local_visible_entities() {
	thread_local visible_entities entities;
	entities.clear();
	return entities;
}