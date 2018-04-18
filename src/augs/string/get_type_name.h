#pragma once
#include <string>

#include "augs/templates/traits/is_std_array.h"

std::string demangle(const char*);

template <class T, class = void>
struct has_custom_type_name : std::false_type {};

template <class T>
struct has_custom_type_name<T, decltype(T::get_custom_type_name(), void())> : std::true_type {};

template <class T>
constexpr bool has_custom_type_name_v = has_custom_type_name<T>::value;

template <class T>
const std::string& get_type_name() {
	static const std::string name = [](){
		if constexpr(is_std_array_v<T>) {
			return get_type_name<typename T::value_type>() + '[' + std::to_string(is_std_array<T>::size) + ']';
		}

		return demangle(typeid(T).name());
	}();

	return name;
}

template <class T>
const std::string& get_type_name_strip_namespace() {
	static const std::string name = []() {
		if constexpr(has_custom_type_name_v<T>) {
			return T::get_custom_type_name();
		}
		else {
			auto name = get_type_name<T>();

			if (const auto it = name.rfind("::");
				it != std::string::npos
			) {
				name = name.substr(it + 2);
			}

			return name;
		}
	}();

	return name;
}

template <class T>
const std::string& get_type_name(const T&) {
	return get_type_name<T>();
}

template <class T>
const std::string& get_type_name_strip_namespace(const T& t) {
	return get_type_name_strip_namespace<std::decay_t<T>>();
}