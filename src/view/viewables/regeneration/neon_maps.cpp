#include <sstream>

#include "augs/filesystem/file.h"
#include "augs/filesystem/directory.h"
#include "augs/misc/simple_pair.h"

#include "augs/ensure.h"
#include "augs/readwrite/memory_stream.h"

#include "augs/image/image.h"

#include "view/viewables/regeneration/neon_maps.h"

#include "augs/readwrite/byte_file.h"

#define PIXEL_NONE rgba(0,0,0,0)

void make_neon(
	const neon_map_input& input,
	augs::image& source
);

void regenerate_neon_map(
	const augs::path_type& input_image_path,
	const augs::path_type& output_image_path,
	const neon_map_input in,
	const bool force_regenerate
) {
	neon_map_stamp new_stamp;
	new_stamp.input = in;
	new_stamp.last_write_time_of_source = augs::last_write_time(input_image_path);

	const auto neon_map_path = output_image_path;
	const auto neon_map_stamp_path = augs::path_type(neon_map_path).replace_extension(".stamp");

	const auto new_stamp_bytes = augs::to_bytes(new_stamp);

	bool should_regenerate = force_regenerate;

	if (!augs::exists(neon_map_path)) {
		should_regenerate = true;
	}
	else {
		if (!augs::exists(neon_map_stamp_path)) {
			should_regenerate = true;
		}
		else {
			const auto existent_stamp_bytes = augs::file_to_bytes(neon_map_stamp_path);
			const bool are_stamps_identical = (new_stamp_bytes == existent_stamp_bytes);

			if (!are_stamps_identical) {
				should_regenerate = true;
			}
		}
	}

	if (should_regenerate) {
		LOG("Regenerating neon map for %x", input_image_path);

		thread_local augs::image source_image;
		source_image.from_file(input_image_path);

		make_neon(new_stamp.input, source_image);

		source_image.save(neon_map_path);

		augs::create_directories_for(neon_map_stamp_path);
		augs::save_as_bytes(new_stamp_bytes, neon_map_stamp_path);
	}
}

void generate_gauss_kernel(
	const neon_map_input& input,
	std::vector<double>& result
);

void scan_and_hide_undesired_pixels(
	augs::image& original_image,
	const std::vector<rgba>& color_whitelist,
	std::vector<vec2u>& target_positions
);

void resize_image(
	augs::image& image_to_resize,
	const vec2u size
);

void cut_empty_edges(augs::image& source);

/* Thread local usage actually makes this slower, I have no idea why. */

void make_neon(
	const neon_map_input& input,
	augs::image& source
) {
	const auto radius = input.radius;
	const auto rows = radius.y;
	const auto cols = radius.x;

	resize_image(source, radius);

	thread_local std::vector<double> kernel_;
	thread_local std::vector<vec2u> pixel_coordinates_;
	thread_local std::vector<rgba> pixels_original_;

	auto& kernel = kernel_;
	auto& pixel_coordinates = pixel_coordinates_;
	auto& pixels_original = pixels_original_; 

	pixel_coordinates.clear();
	pixels_original.clear();

	scan_and_hide_undesired_pixels(source, input.light_colors, pixel_coordinates);
	generate_gauss_kernel(input, kernel);

	for (const auto p : pixel_coordinates) {
		pixels_original.emplace_back(source.pixel(p));
	}

	for (std::size_t i = 0; i < pixel_coordinates.size(); ++i) {
		const auto coord = pixel_coordinates[i];
		const auto center_pixel = pixels_original[i];

		const auto center_pixel_rgba = rgba{ center_pixel[2], center_pixel[1], center_pixel[0], center_pixel[3] };

		for (unsigned y = 0; y < rows; ++y) {
			for (unsigned x = 0; x < cols; ++x) {
				const unsigned current_index_y = coord.y + y - radius.y / 2;

				if (current_index_y >= source.get_rows()) {
					continue;
				}

				const unsigned current_index_x = coord.x + x - radius.x / 2;

				if (current_index_x >= source.get_columns()) {
					continue;
				}

				rgba& current_pixel = source.pixel({ current_index_x, current_index_y });
				const auto current_pixel_rgba = rgba{ current_pixel[2], current_pixel[1], current_pixel[0], current_pixel[3] };
				auto alpha = static_cast<unsigned>(255 * kernel[y * cols + x] * input.amplification);
				alpha = alpha > 255 ? 255 : alpha;

				if (current_pixel_rgba == PIXEL_NONE && alpha) {
					current_pixel[2] = center_pixel[2];
					current_pixel[1] = center_pixel[1];
					current_pixel[0] = center_pixel[0];
				}

				else if (current_pixel_rgba != center_pixel_rgba && alpha) {
					current_pixel[2] = static_cast<rgba_channel>((alpha * center_pixel[2] + current_pixel[3] * current_pixel[2]) / (alpha + current_pixel[3]));
					current_pixel[1] = static_cast<rgba_channel>((alpha * center_pixel[1] + current_pixel[3] * current_pixel[1]) / (alpha + current_pixel[3]));
					current_pixel[0] = static_cast<rgba_channel>((alpha * center_pixel[0] + current_pixel[3] * current_pixel[0]) / (alpha + current_pixel[3]));
				}

				if (alpha > current_pixel[3]) {
					current_pixel[3] = static_cast<rgba_channel>(alpha);
				}
			}
		}
	}

	for (std::size_t i = 0; i < pixel_coordinates.size(); ++i) {
		source.pixel(pixel_coordinates[i]) = pixels_original[i];
	}

	cut_empty_edges(source);

	for (unsigned y = 0; y < source.get_rows(); ++y) {
		for (unsigned x = 0; x < source.get_columns(); ++x) {
			source.pixel({ x, y }).a = std::min(rgba_channel(255), rgba_channel(source.pixel({ x, y }).a * input.alpha_multiplier));
		}
	}
}

void generate_gauss_kernel(const neon_map_input& input, std::vector<double>& result) {
	const auto radius = input.radius;
	const auto rows = radius.y;
	const auto cols = radius.x;
	const auto total_pixels = rows * cols;

	thread_local std::vector<augs::simple_pair<int, int>> index_;
	auto& index = index_;

	index.resize(total_pixels);
	result.resize(total_pixels);

	{
		const auto max_index_x = radius.x / 2;
		const auto max_index_y = radius.y / 2;

		for (unsigned y = 0; y < radius.y; ++y) {
			for (unsigned x = 0; x < radius.x; ++x) {
				index[y * cols + x] = { 
					static_cast<int>(x - max_index_x),
				   	static_cast<int>(y - max_index_y)
				};
			}
		}
	}

	for (unsigned y = 0; y < rows; ++y) {
		for (unsigned x = 0; x < cols; ++x) {
			result[y * cols + x] = exp(-1 * (pow(index[y * cols + x].first, 2) + pow(index[y * cols + x].second, 2)) / 2 / pow(input.standard_deviation, 2)) / PI<float> / 2 / pow(input.standard_deviation, 2);
		}
	}

	double sum = 0.f;

	for (const auto& v : result) {
		sum += v;
	}

	for (auto& v : result) {
		v /= sum;
	}
}


void scan_and_hide_undesired_pixels(
	augs::image& original_image,
	const std::vector<rgba>& color_whitelist,
	std::vector<vec2u>& result
) {
	for (unsigned y = 0; y < original_image.get_rows(); ++y) {
		for (unsigned x = 0; x < original_image.get_columns(); ++x) {
			auto& current_pixel = original_image.pixel({ x, y });

			if (!found_in(color_whitelist, current_pixel)) {
				current_pixel = PIXEL_NONE;
			}
			else {
				result.emplace_back(x, y);
			}
		}
	}
}

/* 
	It appears that moving the result instead of copying it is faster.
*/

void resize_image(
	augs::image& image_to_resize,
	vec2u size
) {
	size.x = image_to_resize.get_columns() + size.x * 2;
	size.y = image_to_resize.get_rows() + size.y * 2;

	augs::image copy_mat;

	copy_mat.resize_fill(size);

	auto offset_x = static_cast<int>(size.x - image_to_resize.get_columns()) / 2;

	if (offset_x < 0) {
		offset_x = 0;
	}

	auto offset_y = static_cast<int>(size.y - image_to_resize.get_rows()) / 2;

	if (offset_y < 0) {
		offset_y = 0;
	}

	for (unsigned y = 0; y < image_to_resize.get_rows(); ++y) {
		for (unsigned x = 0; x < image_to_resize.get_columns(); ++x) {
			copy_mat.pixel({ x + offset_x, y + offset_y }) = image_to_resize.pixel({ x, y });;
		}
	}

	image_to_resize = std::move(copy_mat);
}

void cut_empty_edges(augs::image& source) {
	vec2u output_size = source.get_size();
	vec2u offset = { 0,0 };
	for (unsigned y = 0; y < source.get_rows() / 2; ++y) {
		bool pixel_found = false;
		for (unsigned x = 0; x < source.get_columns(); ++x) {
			if (source.pixel({ x, y }) != PIXEL_NONE || source.pixel({ x, source.get_rows() - y - 1 }) != PIXEL_NONE) {
				pixel_found = true;
				break;
			}
		}
		if (!pixel_found) {
			output_size.y -= 2;
			++offset.y;
		}
		else
			break;
	}

	for (unsigned x = 0; x < source.get_columns() / 2; ++x) {
		bool pixel_found = false;
		for (unsigned y = 0; y < source.get_rows(); ++y) {
			if (source.pixel({ x, y }) != PIXEL_NONE || source.pixel({ source.get_columns() - x - 1, y }) != PIXEL_NONE) {
				pixel_found = true;
				break;
			}
		}
		if (!pixel_found) {
			output_size.x -= 2;
			++offset.x;
		}
		else
			break;
	}

	if (offset == vec2u(0, 0) || output_size.x == 0 || output_size.y == 0) {
		return;
	}

	augs::image copy;

	copy.resize_no_fill(output_size);

	for (unsigned x = 0; x < copy.get_columns(); ++x) {
		for (unsigned y = 0; y < copy.get_rows(); ++y) {
			copy.pixel({ x,y }) = source.pixel({ x + offset.x, y + offset.y });
		}
	}

	source = std::move(copy);
}