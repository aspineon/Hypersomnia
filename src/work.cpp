#if PLATFORM_UNIX
#include <csignal>
#endif

#include <functional>

#include "fp_consistency_tests.h"

#include "augs/log_path_getters.h"
#include "augs/unit_tests.h"
#include "augs/global_libraries.h"

#include "augs/templates/identity_templates.h"
#include "augs/templates/container_templates.h"
#include "augs/templates/history.hpp"
#include "augs/templates/traits/in_place.h"
#include "augs/templates/thread_pool.h"

#include "augs/filesystem/file.h"
#include "augs/filesystem/directory.h"

#include "augs/misc/time_utils.h"
#include "augs/misc/imgui/imgui_utils.h"
#include "augs/misc/lua/lua_utils.h"

#include "augs/graphics/renderer.h"
#include "augs/graphics/renderer_backend.h"

#include "augs/window_framework/shell.h"
#include "augs/window_framework/window.h"
#include "augs/window_framework/platform_utils.h"
#include "augs/audio/audio_context.h"
#include "augs/audio/audio_command_buffers.h"
#include "augs/drawing/drawing.hpp"

#include "game/organization/all_component_includes.h"
#include "game/organization/all_messages_includes.h"
#include "game/detail/inventory/inventory_slot_handle.h"
#include "game/detail/entity_handle_mixins/inventory_mixin.hpp"

#include "game/cosmos/data_living_one_step.h"
#include "game/cosmos/cosmos.h"

#include "view/game_gui/game_gui_system.h"

#include "view/audiovisual_state/world_camera.h"
#include "view/audiovisual_state/audiovisual_state.h"
#include "view/rendering_scripts/illuminated_rendering.h"
#include "view/viewables/images_in_atlas_map.h"
#include "view/viewables/streaming/viewables_streaming.h"
#include "view/frame_profiler.h"

#include "application/session_profiler.h"
#include "application/config_lua_table.h"

#include "application/gui/settings_gui.h"
#include "application/gui/start_client_gui.h"
#include "application/gui/start_server_gui.h"
#include "application/gui/ingame_menu_gui.h"

#include "application/network/network_common.h"
#include "application/setups/all_setups.h"

#include "application/main/imgui_pass.h"
#include "application/main/draw_debug_details.h"
#include "application/main/draw_debug_lines.h"
#include "application/main/release_flags.h"
#include "application/main/flash_afterimage.h"
#include "application/setups/editor/editor_player.hpp"

#include "application/input/input_pass_result.h"

#include "application/setups/draw_setup_gui_input.h"

#include "cmd_line_params.h"
#include "build_info.h"

#include "augs/readwrite/byte_readwrite.h"
#include "view/game_gui/special_indicator_logic.h"
#include "augs/window_framework/create_process.h"
#include "application/setups/editor/editor_popup.h"
#include "application/main/game_frame_buffer.h"
#include "application/main/cached_visibility_data.h"
#include "augs/graphics/frame_num_type.h"
#include "view/rendering_scripts/launch_visibility_jobs.h"
#include "view/rendering_scripts/for_each_vis_request.h"
#include "game/cosmos/for_each_entity.h"
#include "application/setups/client/demo_paths.h"

#include "3rdparty/cpp-httplib/httplib.h"

#define CANON_SHADER_FOLDER "content/necessary/shaders"
#define LOCAL_SHADER_FOLDER CANON_SHADER_FOLDER

std::function<void()> ensure_handler;
bool log_to_live_file = false;
bool is_dedicated_server = false;

/*
	static is used for all variables because some take massive amounts of space.
	They would otherwise cause a stack overflow.
	For example, Windows provides us with mere 1MB of stack space by default.
	
	To preserve the destruction in the order of definition,
	we must also make all other variables static to avoid bugs.

	This function will only be entered ONCE during the lifetime of the program.
*/

int work(const int argc, const char* const * const argv) try {
	setup_float_flags();

	augs::create_directories(LOG_FILES_DIR);
	augs::create_directories(SERVER_LOG_FILES_DIR);

	static const auto canon_config_path = augs::path_type("default_config.lua");
	static const auto local_config_path = augs::path_type(LOCAL_FILES_DIR "/config.lua");

	LOG("Creating lua state.");
	static auto lua = augs::create_lua_state();

	LOG("Loading the config.");

	static const auto canon_config = []() {
		auto result = config_lua_table {
			lua,
			canon_config_path
		};

#if !IS_PRODUCTION_BUILD
#if PLATFORM_UNIX
		/* Some developer-friendly options */
		result.default_client_start.chosen_address_type = connect_address_type::CUSTOM;
		result.window.fullscreen = false;
#endif
#endif

		return result;
	}();

	static auto config = []() {
		if (augs::exists(local_config_path)) {
			auto result = canon_config;
			result.load_patch(lua, local_config_path);

			return result;
		}
		else {
			return canon_config;
		}
	}();

	LOG("Initializing ImGui.");

	static const auto imgui_ini_path = 
		is_dedicated_server ? 
		LOCAL_FILES_DIR "/server_imgui.ini"
		: LOCAL_FILES_DIR "/imgui.ini"
	;

	static const auto imgui_log_path = get_path_in_log_files("imgui_log.txt");

	augs::imgui::init(
		imgui_ini_path,
		imgui_log_path.c_str(),
		config.gui_style
	);

	LOG("Creating the ImGui atlas image.");
	static const auto imgui_atlas_image = augs::imgui::create_atlas_image(config.gui_fonts.gui);

	static augs::graphics::renderer_backend::result_info renderer_backend_result;

	{
		using namespace augs::imgui;

		const auto win_bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
		auto fix_background_color = scoped_style_color(ImGuiCol_WindowBg, ImVec4{win_bg.x, win_bg.y, win_bg.z, 1.f});

		const auto window_size = vec2i(500, 150);
		auto settings = config.window;
		settings.size = window_size;
		settings.fullscreen = false;
		settings.raw_mouse_input = false;
		settings.border = false;

		augs::window window(settings);
		const auto disp = window.get_display();

		settings.position = vec2i { disp.w / 2 - window_size.x / 2, disp.h / 2 - window_size.y / 2 };
		window.apply(settings);

		augs::graphics::renderer_backend renderer_backend;

		const auto imgui_atlas = augs::graphics::texture(imgui_atlas_image);

		augs::local_entropy entropy;

		augs::renderer renderer;

		optional_shader standard;

		LOG("Initializing the standard shader.");

		try {
			const auto canon_vsh_path = typesafe_sprintf("%x/%x.vsh", CANON_SHADER_FOLDER, "standard");
			const auto canon_fsh_path = typesafe_sprintf("%x/%x.fsh", CANON_SHADER_FOLDER, "standard");

			standard.emplace(
				canon_vsh_path,
				canon_fsh_path
			);
		}
		catch (const augs::graphics::shader_error& err) {
			(void)err;
		}

		if (standard != std::nullopt) {
			standard->set_as_current(renderer);
			standard->set_projection(renderer, augs::orthographic_projection(vec2(window_size)));
		}

		renderer.set_viewport({ vec2i{0, 0}, window_size });

		augs::timer frame_timer;

		float cntr = 0.f;

		while (cntr < 1.f) {
			window.collect_entropy(entropy);

			for (const auto& e : entropy) {
				if (e.is_exit_message()) {
					return EXIT_SUCCESS;
				}
			}

			const auto frame_delta = frame_timer.extract_delta();
			const auto dt_secs = frame_delta.in_seconds();

			augs::imgui::setup_input(
				entropy,
				dt_secs,
				window_size
			);

			ImGui::NewFrame();
			center_next_window(1.f, ImGuiCond_Always);

			cntr += dt_secs;

			{
				auto loading_window = scoped_window("Loading in progress", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

				text_color("Upgrading Hypersomnia...", yellow);
				ImGui::Separator();

				if (cntr >= 0.f) {
					ImGui::ProgressBar(cntr, ImVec2(-1.0f,0.0f));
				}
			}

			augs::imgui::render();

			renderer.clear_current_fbo();
			renderer.draw_call_imgui(
				imgui_atlas, 
				nullptr, 
				nullptr, 
				nullptr
			);

			renderer_backend_result.clear();

			{
				auto& r = renderer;

				renderer_backend.perform(
					renderer_backend_result,
					r.commands.data(),
					r.commands.size(),
					r.dedicated
				);
			}

			for (const auto& f : renderer_backend_result.imgui_lists_to_delete) {
				IM_DELETE(f);
			}

			window.swap_buffers();

			renderer.next_frame();
		}
	}

	static augs::timer until_first_swap;
	bool until_first_swap_measured = false;

#if PLATFORM_UNIX	
	static volatile std::sig_atomic_t signal_status = 0;
 
	static auto signal_handler = [](const int signal_type) {
   		signal_status = signal_type;
	};

	std::signal(SIGINT, signal_handler);
	std::signal(SIGTERM, signal_handler);
	std::signal(SIGSTOP, signal_handler);
#endif

	static session_profiler render_thread_performance;
	static network_profiler network_performance;
	static network_info network_stats;
	static server_network_info server_stats;

	LOG("If the game crashes repeatedly, consider deleting the \"cache\" folder.\n");
	LOG("Started at %x", augs::date_time().get_readable());
	LOG("Working directory: %x", augs::get_current_working_directory());

	LOG("Creating directories: %x %x", GENERATED_FILES_DIR, LOCAL_FILES_DIR);

	augs::create_directories(GENERATED_FILES_DIR);
	augs::create_directories(LOCAL_FILES_DIR);
	augs::create_directories(DEMOS_DIR);

	dump_detailed_sizeof_information(get_path_in_log_files("detailed_sizeofs.txt"));

	static const auto float_tests_succeeded = 
		config.perform_float_consistency_test 
		? perform_float_consistency_tests() 
		: true
	;

	if (config.log_to_live_file) {
		augs::remove_file(get_path_in_log_files("live_debug.txt"));

		log_to_live_file = true;

		LOG("Live log was enabled due to a flag in config.");
		LOG("Live log file created at %x", augs::date_time().get_readable());
	}

	static auto last_saved_config = config;

	static auto change_with_save = [&](auto setter) {
		setter(config);
		setter(last_saved_config);

		last_saved_config.save_patch(lua, canon_config, local_config_path);
	};

	static auto last_exit_incorrect_popup = std::optional<editor_popup>();

	static auto perform_last_exit_incorrect = [&]() {
		if (last_exit_incorrect_popup != std::nullopt) {
			if (last_exit_incorrect_popup->perform()) {
				last_exit_incorrect_popup = std::nullopt;
			}
		}
	};

	if (const auto last_failure_log = find_last_incorrect_exit(); last_failure_log.size() > 0) {
		change_with_save([](config_lua_table& cfg) {
			cfg.launch_mode = launch_type::MAIN_MENU;
		});

		last_exit_incorrect_popup = editor_popup {
			"Warning",
			typesafe_sprintf(R"(Looks like the game has crashed since the last time.
Consider sending developers the log file located at:

%x

If you experience repeated crashes, 
you might try resetting all your settings,
which can be done by pressing "Reset to factory default" in Settings->General,
and then hitting Save settings.

)", last_failure_log),
			""
		};
	}

	LOG("Parsing command-line parameters.");
	static const auto params = cmd_line_params(argc, argv);

	static augs::timer global_libraries_timer;

	LOG("Initializing global libraries");

	static const auto libraries = 
		params.start_dedicated_server 
		? augs::global_libraries({}) 
		: augs::global_libraries(augs::global_libraries::library::FREETYPE) 
	;

	LOG("Initializing global libraries took: %x ms", global_libraries_timer.template extract<std::chrono::milliseconds>());

	static std::optional<setup_variant> current_setup;

	static auto has_current_setup = []() {
		return current_setup != std::nullopt;
	};

	static auto emplace_current_setup = [&p = current_setup] (auto tag, auto&&... args) {
		using Tag = decltype(tag);
		using T = type_of_in_place_type_t<Tag>; 

		if (p == std::nullopt) {
			p.emplace(
				tag,
				std::forward<decltype(args)>(args)...
			);
		}
		else {
			p.value().emplace<T>(std::forward<decltype(args)>(args)...);
		}
	};

	if (config.unit_tests.run) {
		/* Needed by some unit tests */
		augs::network_raii raii;

		LOG("Running unit tests.");
		augs::run_unit_tests(config.unit_tests);

		LOG("All unit tests have passed.");

		if (params.unit_tests_only) {
			return EXIT_SUCCESS;
		}
	}
	else {
		LOG("Unit tests were disabled.");
	}

	if (params.start_dedicated_server) {
		LOG("Starting the dedicated server at port: %x", config.default_server_start.port);

		emplace_current_setup(
			std::in_place_type_t<server_setup>(),
			lua,
			config.default_server_start,
			config.server,
			config.server_solvable,
			config.client,
			config.private_server,
			config.dedicated_server
		);

		auto& server = std::get<server_setup>(*current_setup);

		while (server.is_running()) {
#if PLATFORM_UNIX
			if (signal_status != 0) {
				const auto sig = signal_status;

				LOG("%x received.", strsignal(sig));

				if(
					sig == SIGINT
					|| sig == SIGSTOP
					|| sig == SIGTERM
				) {
					LOG("Gracefully shutting down.");
					break;
				}
			}
#endif

			const auto zoom = 1.f;

			server.advance(
				{
					vec2i(),
					config.input,
					zoom,
					network_performance,
					server_stats
				},
				solver_callbacks()
			);

			server.sleep_until_next_tick();
		}

		return EXIT_SUCCESS;
	}

	LOG("Initializing the audio context.");

	static augs::audio_context audio(config.audio);

	LOG("Logging all audio devices.");
	augs::log_all_audio_devices(get_path_in_log_files("audio_devices.txt"));

	static auto thread_pool = augs::thread_pool(config.performance.get_num_pool_workers());
	static augs::audio_command_buffers audio_buffers(thread_pool);

	LOG("Initializing the window.");
	static augs::window window(config.window);

	LOG("Initializing the renderer backend.");
	static augs::graphics::renderer_backend renderer_backend;

	static game_frame_buffer_swapper buffer_swapper;

	static auto get_read_buffer = []() -> game_frame_buffer& {
		return buffer_swapper.get_read_buffer();
	};

	static auto get_write_buffer = []() -> game_frame_buffer& {
		return buffer_swapper.get_write_buffer();
	};

	get_write_buffer().screen_size = window.get_screen_size();
	get_read_buffer().new_settings = config.window;
	get_read_buffer().swap_when = config.performance.swap_window_buffers_when;

	static auto logic_get_screen_size = []() {
		return get_write_buffer().screen_size;
	};

	static auto get_general_renderer = []() -> augs::renderer& {
		return get_write_buffer().renderers.all[renderer_type::GENERAL];
	};

	LOG_NVPS(renderer_backend.get_max_texture_size());

	LOG("Initializing the necessary fbos.");
	static all_necessary_fbos necessary_fbos(
		logic_get_screen_size(),
		config.drawing
	);

	LOG("Initializing the necessary shaders.");
	static all_necessary_shaders necessary_shaders(
		get_general_renderer(),
		CANON_SHADER_FOLDER,
		LOCAL_SHADER_FOLDER,
		config.drawing
	);

	LOG("Initializing the necessary sounds.");
	static all_necessary_sounds necessary_sounds(
		"content/necessary/sfx"
	);

	LOG("Initializing the necessary image definitions.");
	static const necessary_image_definitions_map necessary_image_definitions(
		lua,
		"content/necessary/gfx",
		config.content_regeneration.regenerate_every_time
	);

	LOG("Creating the ImGui atlas.");
	static const auto imgui_atlas = augs::graphics::texture(imgui_atlas_image);

	static const auto configurables = configuration_subscribers {
		window,
		necessary_fbos,
		audio,
		get_general_renderer()
	};

	static atlas_profiler atlas_performance;
	static frame_profiler game_thread_performance;

	/* 
		unique_ptr is used to avoid stack overflow.

		Main menu setup state may be preserved, 
		therefore it resides in a separate unique_ptr.
	*/

	static std::optional<main_menu_setup> main_menu;

	static auto has_main_menu = []() {
		return main_menu != std::nullopt;
	};

	static auto emplace_main_menu = [&p = main_menu] (auto&&... args) {
		if (p == std::nullopt) {
			p.emplace(std::forward<decltype(args)>(args)...);
		}
	};

	static settings_gui_state settings_gui = std::string("Settings");
	static start_client_gui_state start_client_gui = std::string("Connect to server");
	static start_server_gui_state start_server_gui = std::string("Host a server");

	static ingame_menu_gui ingame_menu;

	/*
		Runtime representations of viewables,
		loaded from the definitions provided by the current setup.
		The setup's chosen viewables_loading_type decides if they are 
		loaded just once or if they are for example continuously streamed.
	*/

	LOG("Initializing the streaming of viewables.");
	static viewables_streaming streaming;

	static auto get_blank_texture = []() {
		return streaming.necessary_images_in_atlas[assets::necessary_image_id::BLANK];
	};

	static auto get_drawer_for = [&](augs::renderer& chosen_renderer) { 
		return augs::drawer_with_default {
			chosen_renderer.get_triangle_buffer(),
			get_blank_texture()
		};
	};

	auto streaming_finalize = augs::scope_guard([&]() {
		streaming.finalize_pending_tasks();
	});

	static world_camera gameplay_camera;
	LOG("Initializing the audiovisual state.");
	static audiovisual_state audiovisuals;
	
	static auto get_audiovisuals = []() -> audiovisual_state& {
		return audiovisuals;
	};


	/*
		The lambdas that aid to make the main loop code more concise.
	*/	

	static auto visit_current_setup = [&](auto callback) -> decltype(auto) {
		if (has_current_setup()) {
			return std::visit(
				[&](auto& setup) -> decltype(auto) {
					return callback(setup);
				}, 
				*current_setup
			);
		}
		else {
			return callback(*main_menu);
		}
	};

	static auto setup_requires_cursor = []() {
		return visit_current_setup([&](const auto& s) {
			return s.requires_cursor();
		});
	};

	static auto get_interpolation_ratio = []() {
		return visit_current_setup([](auto& setup) {
			return setup.get_interpolation_ratio();
		});
	};

	static auto on_specific_setup = [&](auto callback) -> decltype(auto) {
		using T = remove_cref<argument_t<decltype(callback), 0>>;

		if constexpr(std::is_same_v<T, main_menu_setup>) {
			if (has_main_menu()) {
				callback(*main_menu);
			}
		}
		else {
			if (has_current_setup()) {
				if (auto* setup = std::get_if<T>(&*current_setup)) {
					callback(*setup);
				}
			}
		}
	};

	static auto get_unofficial_content_dir = [&]() {
		return visit_current_setup([](const auto& s) { return s.get_unofficial_content_dir(); });
	};

	static auto get_render_layer_filter = [&]() {
		return visit_current_setup([](const auto& s) { return s.get_render_layer_filter(); });
	};

	/* TODO: We need to have one game gui per cosmos. */
	static game_gui_system game_gui;
	static bool game_gui_mode_flag = false;

	static std::atomic<augs::frame_num_type> current_frame = 0;

	static auto load_all = [&](const all_viewables_defs& new_defs) {
		const auto frame_num = current_frame.load();

		std::optional<arena_player_metas> new_player_metas;

		if (streaming.finished_loading_player_metas(frame_num)) {
			visit_current_setup([&](auto& setup) {
				new_player_metas = setup.get_new_player_metas();
			});
		}

		streaming.load_all({
			frame_num,
			new_defs,
			necessary_image_definitions,
			config.gui_fonts,
			config.content_regeneration,
			get_unofficial_content_dir(),
			get_general_renderer(),
			renderer_backend.get_max_texture_size(),

			new_player_metas
		});
	};

	static auto setup_launcher = [&](auto&& setup_init_callback) {
		game_gui_mode_flag = false;

		audio_buffers.finish();
		audio_buffers.stop_all_sources();

		get_audiovisuals().get<sound_system>().clear();

		network_stats = {};
		server_stats = {};

		main_menu.reset();
		current_setup.reset();
		ingame_menu.show = false;

		setup_init_callback();
		
		visit_current_setup([&](const auto& setup) {
			using T = remove_cref<decltype(setup)>;
			
			if constexpr(T::loading_strategy == viewables_loading_type::LOAD_ALL_ONLY_ONCE) {
				load_all(setup.get_viewable_defs());
			}
		});
	};

	static auto launch_editor = [&](auto&&... args) {
		setup_launcher([&]() {
			emplace_current_setup(std::in_place_type_t<editor_setup>(),
				std::forward<decltype(args)>(args)...
			);
		});
	};

	static auto launch_setup = [&](const launch_type mode) {
		LOG("Launched mode: %x", augs::enum_to_string(mode));

		get_audiovisuals().get<particles_simulation_system>().clear();
		
		change_with_save([mode](config_lua_table& cfg) {
			cfg.launch_mode = mode;
		});

		switch (mode) {
			case launch_type::MAIN_MENU:
				setup_launcher([&]() {
					if (!has_main_menu()) {
						emplace_main_menu(lua, config.main_menu);
					}
				});

				break;

			case launch_type::CLIENT:
				setup_launcher([&]() {
					emplace_current_setup(std::in_place_type_t<client_setup>(),
						lua,
						config.default_client_start,
						config.client
					);
				});

				break;

			case launch_type::SERVER:
				setup_launcher([&]() {
					emplace_current_setup(std::in_place_type_t<server_setup>(),
						lua,
						config.default_server_start,
						config.server,
						config.server_solvable,
						config.client,
						config.private_server,
						std::nullopt
					);
				});

				break;

			case launch_type::EDITOR:
				launch_editor(lua);

				break;

			case launch_type::TEST_SCENE:
				setup_launcher([&]() {
					emplace_current_setup(std::in_place_type_t<test_scene_setup>(),
						lua,
						config.test_scene,
						config.get_input_recording_mode()
					);
				});

				break;

			default:
				ensure(false && "The launch_setup mode you have chosen is currently out of service.");
				break;
		}
	};

	static bool client_start_requested = false;
	static bool server_start_requested = false;

	static auto perform_start_client = [](const auto frame_num) {
		const bool perform_result = start_client_gui.perform(
			frame_num,
			get_general_renderer(), 
			streaming.avatar_preview_tex, 
			window, 
			config.default_client_start, 
			config.client,
			config.official_arena_servers
		);

		if (perform_result || client_start_requested) {
			client_start_requested = false;

			change_with_save(
				[&](auto& cfg) {
					cfg.default_client_start = config.default_client_start;
					cfg.client = config.client;
				}
			);

			launch_setup(launch_type::CLIENT);
		}
	};

	static auto perform_start_server = []() {
		if (start_server_gui.perform(config.default_server_start) || server_start_requested) {
			server_start_requested = false;

			change_with_save(
				[&](auto& cfg) {
					cfg.default_server_start = config.default_server_start;
					cfg.client = config.client;
				}
			);

			if (start_server_gui.instance_type == server_instance_type::INTEGRATED) {
				launch_setup(launch_type::SERVER);
			}
			else {
				augs::spawn_detached_process(params.exe_path.string(), "--dedicated-server");
				config.default_client_start.set_custom(typesafe_sprintf("%x:%x", config.default_server_start.ip, config.default_server_start.port));

				launch_setup(launch_type::CLIENT);
			}
		}
	};

	static auto get_viewable_defs = [&]() -> const all_viewables_defs& {
		return visit_current_setup([](auto& setup) -> const all_viewables_defs& {
			return setup.get_viewable_defs();
		});
	};

	static auto create_game_gui_deps = [&](const config_lua_table& viewing_config) {
		return game_gui_context_dependencies {
			get_viewable_defs().image_definitions,
			streaming.images_in_atlas,
			streaming.necessary_images_in_atlas,
			streaming.get_loaded_gui_fonts().gui,
			get_audiovisuals().randomizing,
			viewing_config.game_gui
		};
	};

	static auto create_menu_context_deps = [&](const config_lua_table& viewing_config) {
		return menu_context_dependencies{
			streaming.necessary_images_in_atlas,
			streaming.get_loaded_gui_fonts().gui,
			necessary_sounds,
			viewing_config.audio_volume
		};
	};

	static auto get_game_gui_subject = [&]() -> const_entity_handle {
		const auto& viewed_cosmos = visit_current_setup([](auto& setup) -> const cosmos& {
			return setup.get_viewed_cosmos();
		});

		const auto gui_character_id = visit_current_setup([](auto& setup) {
			return setup.get_game_gui_subject_id();
		});

		return viewed_cosmos[gui_character_id];
	};

	static auto get_viewed_character = [&]() -> const_entity_handle {
		const auto& viewed_cosmos = visit_current_setup([](auto& setup) -> const cosmos& {
			return setup.get_viewed_cosmos();
		});

		const auto viewed_character_id = visit_current_setup([](auto& setup) {
			return setup.get_viewed_character_id();
		});

		return viewed_cosmos[viewed_character_id];
	};

	static auto get_controlled_character = [&]() -> const_entity_handle {
		const auto& viewed_cosmos = visit_current_setup([](auto& setup) -> const cosmos& {
			return setup.get_viewed_cosmos();
		});

		const auto controlled_character_id = visit_current_setup([](auto& setup) {
			return setup.get_controlled_character_id();
		});

		return viewed_cosmos[controlled_character_id];
	};
		
	static auto should_draw_game_gui = [&]() {
		{
			bool should = true;

			on_specific_setup([&](editor_setup& setup) {
				if (!setup.anything_opened() || setup.is_editing_mode()) {
					should = false;
				}
			});

			if (has_main_menu() && !has_current_setup()) {
				should = false;
			}

			if (!should) {
				return false;
			}
		}

		const auto viewed = get_game_gui_subject();

		if (!viewed.alive()) {
			return false;
		}

		if (!viewed.has<components::item_slot_transfers>()) {
			return false;
		}

		return true;
	};

	static auto get_camera_eye = [&]() {		
		if(const auto custom = visit_current_setup(
			[](const auto& setup) { 
				return setup.find_current_camera_eye(); 
			}
		)) {
			return *custom;
		}
		
		if (get_viewed_character().dead()) {
			return camera_eye();
		}

		return gameplay_camera.get_current_eye();
	};

	static auto get_camera_cone = []() {		
		return camera_cone(get_camera_eye(), logic_get_screen_size());
	};

	static auto get_queried_cone = [](const config_lua_table& config) {		
		const auto query_mult = config.session.camera_query_aabb_mult;

		const auto queried_cone = [&]() {
			auto c = get_camera_cone();
			c.eye.zoom /= query_mult;
			return c;
		}();

		return queried_cone;
	};

	static auto get_setup_customized_config = [&]() {
		return visit_current_setup([&](auto& setup) {
			auto config_copy = config;

			/*
				For example, the main menu might want to disable HUD or tune down the sound effects.
				Editor might want to change the window name to the current file.
			*/

			setup.customize_for_viewing(config_copy);
			setup.apply(config_copy);

			if (get_camera_eye().zoom < 1.f) {
				/* Force linear filtering when zooming out */
				config_copy.renderer.default_filtering = augs::filtering_type::LINEAR;
			}

			return config_copy;
		});
	};

	static auto perform_setup_custom_imgui = [&]() {
		/*
			The editor setup might want to use IMGUI to create views of entities or resources,
			thus we ask the current setup for its custom ImGui logic.

			Similarly, client and server setups might want to perform ImGui for things like team selection.
		*/

		visit_current_setup([&](auto& setup) {
			const auto result = setup.perform_custom_imgui({ 
				lua, window, streaming.images_in_atlas, config 
			});

			if (result == custom_imgui_result::GO_TO_MAIN_MENU) {
				launch_setup(launch_type::MAIN_MENU);
			}

			using S = remove_cref<decltype(setup)>;

			if constexpr(std::is_same_v<S, client_setup>) {
				if (result == custom_imgui_result::RETRY) {
					launch_setup(launch_type::CLIENT);
				}
			}
		});
	};

	static auto do_imgui_pass = [](const auto frame_num, auto& new_window_entropy, const auto& frame_delta, const bool in_direct_gameplay) {
		perform_imgui_pass(
			new_window_entropy,
			logic_get_screen_size(),
			frame_delta,
			canon_config,
			config,
			last_saved_config,
			local_config_path,
			settings_gui,
			audio,
			lua,
			[&]() {
				if (!has_current_setup()) {
					perform_start_client(frame_num);
					perform_start_server();
				}

				streaming.display_loading_progress();

				perform_setup_custom_imgui();
				perform_last_exit_incorrect();
			},

			/* Flags controlling IMGUI behaviour */

			ingame_menu.show,
			has_current_setup(),

			in_direct_gameplay,
			float_tests_succeeded
		);
	};

	static auto decide_on_cursor_clipping = [](const bool in_direct_gameplay, const auto& cfg) {
		get_write_buffer().should_clip_cursor = (
			in_direct_gameplay
			|| (
				cfg.window.is_raw_mouse_input()
#if TODO
				&& !cfg.session.use_system_cursor_for_gui
#endif
			)
		);
	};

	static auto get_current_input_settings = [&](const auto& cfg) {
		auto settings = cfg.input;

		on_specific_setup([&](client_setup& setup) {
			settings.character = setup.get_current_requested_settings().public_settings.character_input;
		});

		return settings;
	};

	static auto handle_app_intent = [&](const app_intent_type intent) {
		using T = decltype(intent);

		switch (intent) {
			case T::SWITCH_DEVELOPER_CONSOLE: {
				change_with_save([](config_lua_table& cfg) {
					bool& f = cfg.session.show_developer_console;
					f = !f;
				});

				break;
			}

			default: break;
		}
	};
	
	static auto handle_general_gui_intent = [&](const general_gui_intent_type intent) {
		using T = decltype(intent);

		switch (intent) {
			case T::CLEAR_DEBUG_LINES:
				DEBUG_PERSISTENT_LINES.clear();
				return true;

			case T::SWITCH_WEAPON_LASER: {
				bool& f = config.drawing.draw_weapon_laser;
				f = !f;
				return true;
			}

			case T::TOGGLE_MOUSE_CURSOR: {
				bool& f = game_gui_mode_flag;
				f = !f;
				return true;
			}
			
			default: return false;
		}
	};
 
	static auto main_ensure_handler = []() {
		visit_current_setup(
			[&](auto& setup) {
				setup.ensure_handler();
			}
		);
	};

	::ensure_handler = main_ensure_handler;

	static bool should_quit = false;

	static augs::event::state common_input_state;

	static void(*request_quit)() = nullptr;

	static auto do_main_menu_option = [&](const main_menu_button_type t) {
		using T = decltype(t);

		switch (t) {
			case T::CONNECT_TO_OFFICIAL_UNIVERSE:
				start_client_gui.open();

				if (common_input_state[augs::event::keys::key::LSHIFT]) {
					client_start_requested = true;
				}
				else {
					config.default_client_start.chosen_address_type = connect_address_type::OFFICIAL;
				}

				break;

			case T::CONNECT_TO_UNIVERSE:
				start_client_gui.open();

				if (common_input_state[augs::event::keys::key::LSHIFT]) {
					client_start_requested = true;
				}

				config.default_client_start.chosen_address_type = connect_address_type::CUSTOM;

				break;
				
			case T::HOST_UNIVERSE:
				start_server_gui.open();

				if (common_input_state[augs::event::keys::key::LSHIFT]) {
					server_start_requested = true;
				}

				break;

			case T::LOCAL_UNIVERSE:
				launch_setup(launch_type::TEST_SCENE);
				break;

			case T::EDITOR:
				launch_setup(launch_type::EDITOR);
				break;

			case T::SETTINGS:
				settings_gui.open();
				break;

			case T::CREATORS:
				main_menu->launch_creators_screen();
				break;

			case T::QUIT:
				request_quit();
				break;

			default: break;
		}
	};

	static auto do_ingame_menu_option = [&](const ingame_menu_button_type t) {
		using T = decltype(t);

		switch (t) {
			case T::RESUME:
				ingame_menu.show = false;
				break;

			case T::QUIT_TO_MENU:
				launch_setup(launch_type::MAIN_MENU);
				break;

			case T::SETTINGS:
				settings_gui.open();
				break;

			case T::QUIT:
				request_quit();
				break;

			default: break;
		}
	};

	static auto setup_pre_solve = [&](auto...) {
		get_general_renderer().save_debug_logic_step_lines_for_interpolation(DEBUG_LOGIC_STEP_LINES);
		DEBUG_LOGIC_STEP_LINES.clear();
	};

	/* 
		The audiovisual_step, advance_setup and advance_current_setup lambdas
		are separated only because MSVC outputs ICEs if they become nested.
	*/

	static visible_entities all_visible;

	static auto get_character_camera = [&]() -> character_camera {
		return { get_viewed_character(), { get_camera_eye(), logic_get_screen_size() } };
	};

	static auto reacquire_visible_entities = [](
		const vec2i& screen_size,
		const const_entity_handle& viewed_character,
		const config_lua_table& viewing_config
	) {
		auto scope = measure_scope(game_thread_performance.camera_visibility_query);

		auto queried_eye = get_camera_eye();
		queried_eye.zoom /= viewing_config.session.camera_query_aabb_mult;

		const auto queried_cone = camera_cone(queried_eye, screen_size);

		all_visible.reacquire_all_and_sort({ 
			viewed_character.get_cosmos(), 
			queried_cone, 
			accuracy_type::PROXIMATE,
			get_render_layer_filter(),
			tree_of_npo_filter::all()
		});

		game_thread_performance.num_visible_entities.measure(all_visible.count_all());
	};

	static auto calc_pre_step_crosshair_displacement = [&](const config_lua_table& viewing_config) {
		if (get_viewed_character() != get_controlled_character()) {
			return vec2::zero;
		}

		return visit_current_setup([&](const auto& setup) {
			using T = remove_cref<decltype(setup)>;

			if constexpr(!std::is_same_v<T, main_menu_setup>) {
				const auto& total_collected = setup.get_entropy_accumulator();

				const auto input_cfg = get_current_input_settings(viewing_config);

				if (const auto motion = total_collected.calc_motion(
					get_viewed_character(), 
					game_motion_type::MOVE_CROSSHAIR,
					entropy_accumulator::input {
						input_cfg, 
						logic_get_screen_size(), 
						get_camera_eye().zoom 
					}
				)) {
					return vec2(motion->offset) * input_cfg.character.crosshair_sensitivity;
				}
			}

			return vec2::zero;
		});
	};

	static bool pending_new_state_sample = true;
	static auto last_sampled_cosmos = cosmos_id_type(-1);

	static auto audiovisual_step = [&](
		const augs::audio_renderer* audio_renderer,
		const augs::delta frame_delta,
		const double speed_multiplier,
		const config_lua_table& viewing_config
	) {
		const auto screen_size = logic_get_screen_size();
		const auto viewed_character = get_viewed_character();
		const auto& cosm = viewed_character.get_cosmos();
		
		//get_audiovisuals().reserve_caches_for_entities(viewed_character.get_cosmos().get_solvable().get_entity_pool().capacity());
		
		auto& interp = get_audiovisuals().get<interpolation_system>();

		{
			auto scope = measure_scope(get_audiovisuals().performance.interpolation);

			if (pending_new_state_sample) {
				interp.update_desired_transforms(cosm);
			}

			interp.integrate_interpolated_transforms(
				viewing_config.interpolation, 
				cosm, 
				augs::delta(frame_delta) *= speed_multiplier, 
				cosm.get_fixed_delta()
			);
		}

		gameplay_camera.tick(
			screen_size,
			viewing_config.drawing.fog_of_war,
			interp,
			frame_delta,
			viewing_config.camera,
			viewed_character,
			calc_pre_step_crosshair_displacement(viewing_config)
		);

		reacquire_visible_entities(screen_size, viewed_character, viewing_config);

		const auto inv_tickrate = visit_current_setup([](const auto& setup) {
			return setup.get_inv_tickrate();
		});

		static augs::timer state_changed_timer;

		visit_current_setup([&](const auto& setup) {
			using S = remove_cref<decltype(setup)>;

			const auto now_sampled_cosmos = cosm.get_cosmos_id();

			auto resample = [&]() {
				audio_buffers.finish();
				audio_buffers.stop_all_sources();

				get_audiovisuals().get<sound_system>().clear();
				get_audiovisuals().get<particles_simulation_system>().clear();

				last_sampled_cosmos = now_sampled_cosmos;

				cosm.mark_as_resampled();

#if !IS_PRODUCTION_BUILD
				LOG("Now sampled cosmos has changed.");
#endif
			};

			auto resample_if_different = [&]() {
				const bool requested_resample = cosm.resample_requested();

				if (requested_resample || last_sampled_cosmos != now_sampled_cosmos) {
					resample();
				}
			};

			if constexpr(std::is_same_v<S, client_setup>) {
				const auto& referential = setup.get_arena_handle(client_arena_type::REFERENTIAL).get_cosmos();
				const auto& predicted = setup.get_arena_handle(client_arena_type::PREDICTED).get_cosmos();

				if (referential.resample_requested() || predicted.resample_requested()) {
					resample();

					referential.mark_as_resampled();
					predicted.mark_as_resampled();
				}
			}
			else {
				resample_if_different();
			}
		});

		get_audiovisuals().advance(audiovisual_advance_input {
			audio_buffers,
			audio_renderer,
			frame_delta,
			speed_multiplier,
			inv_tickrate,

			get_character_camera(),
			get_queried_cone(viewing_config),
			all_visible,

			get_viewable_defs().particle_effects,
			cosm.get_logical_assets().plain_animations,

			streaming.loaded_sounds,

			viewing_config.audio_volume,
			viewing_config.sound,
			viewing_config.performance,

			streaming.images_in_atlas,
			get_write_buffer().particle_buffers,
			get_general_renderer().dedicated,
			pending_new_state_sample ? state_changed_timer.extract_delta() : std::optional<augs::delta>(),

			thread_pool
		});

		pending_new_state_sample = false;
	};

	static auto setup_post_solve = [&](
		const const_logic_step step, 
		const augs::audio_renderer* audio_renderer,
		const config_lua_table& viewing_config,
		const audiovisual_post_solve_settings settings
	) {
		pending_new_state_sample = true;

		{
			const auto& defs = get_viewable_defs();

			get_audiovisuals().standard_post_solve(step, { 
				audio_renderer,
				defs.particle_effects, 
				streaming.loaded_sounds,
				viewing_config.audio_volume,
				viewing_config.sound,
				get_character_camera(),
				viewing_config.performance,
				settings
			});
		}

		game_gui.standard_post_solve(
			step, 
			{ settings.prediction }
		);
	};

	static auto setup_post_cleanup = [&](const const_logic_step step) {
		(void)step;
	};

	static auto advance_setup = [&](
		const augs::audio_renderer* audio_renderer,
		const augs::delta frame_delta,
		auto& setup,
		const input_pass_result& result
	) {
		const config_lua_table& viewing_config = result.viewing_config;

		setup.control(result.motions);
		setup.control(result.intents);

		setup.accept_game_gui_events(game_gui.get_and_clear_pending_events());
		
		auto setup_audiovisual_post_solve = [&](const const_logic_step step, const audiovisual_post_solve_settings settings = {}) {
			setup_post_solve(step, audio_renderer, viewing_config, settings);
		};

		{
			using S = remove_cref<decltype(setup)>;

			auto callbacks = solver_callbacks(
				setup_pre_solve,
				setup_audiovisual_post_solve,
				setup_post_cleanup
			);

			const auto zoom = get_camera_eye().zoom;
			const auto input_cfg = get_current_input_settings(viewing_config);

			if constexpr(std::is_same_v<S, client_setup>) {
				/* The client needs more goodies */

				setup.advance(
					{ 
						logic_get_screen_size(), 
						input_cfg, 
						zoom,
						viewing_config.simulation_receiver, 
						viewing_config.lag_compensation, 
						network_performance,
						network_stats,
						get_audiovisuals().get<interpolation_system>(),
						get_audiovisuals().get<past_infection_system>()
					},
					callbacks
				);
			}
			else if constexpr(std::is_same_v<S, server_setup>) {
				setup.advance(
					{ 
						logic_get_screen_size(), 
						input_cfg, 
						zoom,
						network_performance,
						server_stats
					},
					callbacks
				);
			}
			else {
				if constexpr(std::is_same_v<S, editor_setup>) {
					if (setup.is_editing_mode()) {
						pending_new_state_sample = true;
					}
				}

				setup.advance(
					{ 
						frame_delta, 
						logic_get_screen_size(), 
						input_cfg, 
						zoom 
					},
					callbacks
				);
			}
		}

		get_audiovisuals().randomizing.last_frame_delta = frame_delta;
		audiovisual_step(audio_renderer, frame_delta, setup.get_audiovisual_speed(), viewing_config);
	};

	static auto advance_current_setup = [&](
		const augs::audio_renderer* audio_renderer,
		const augs::delta frame_delta,
		const input_pass_result& result
	) { 
		visit_current_setup(
			[&](auto& setup) {
				advance_setup(audio_renderer, frame_delta, setup, result);
			}
		);
	};

	if (!params.editor_target.empty()) {
		launch_editor(lua, params.editor_target);
	}
	else if (params.start_server) {
		launch_setup(launch_type::SERVER);
	}
	else if (params.should_connect) {
		{
			const auto& target = params.connect_address;

			if (!target.empty()) {
				change_with_save([&](config_lua_table& cfg) {
					cfg.default_client_start.set_custom(target);
				});
			}
		}

		launch_setup(launch_type::CLIENT);
	}
	else {
		launch_setup(config.get_launch_mode());
	}

	/* 
		The main loop variables.
	*/

	static augs::timer frame_timer;
	
	static release_flags releases;

	static auto make_create_game_gui_context = [&](const config_lua_table& viewing_config) {
		return [&]() {
			return game_gui.create_context(
				logic_get_screen_size(),
				common_input_state,
				get_game_gui_subject(),
				create_game_gui_deps(viewing_config)
			);
		};
	};

	static auto make_create_menu_context = [&](const config_lua_table& cfg) {
		return [&](auto& gui) {
			return gui.create_context(
				logic_get_screen_size(),
				common_input_state,
				create_menu_context_deps(cfg)
			);
		};
	};

	static auto let_imgui_hijack_mouse = [](auto&& create_game_gui_context, auto&& create_menu_context) {
		if (!ImGui::GetIO().WantCaptureMouse) {
			return;
		}

		/*
			Since ImGUI has quite a different philosophy about input,
			we will need some ugly inter-op with our GUIs.

			If mouse enters any IMGUI element, rewrite ImGui's mouse position to common_input_state.

			This allows us to keep common_input_state up to date, 
			because mousemotions are eaten from the vector already due to ImGui wanting mouse.
		*/

		common_input_state.mouse.pos = ImGui::GetIO().MousePos;

		/* Neutralize hovers on all GUIs whose focus may have just been stolen. */

		game_gui.world.unhover_and_undrag(create_game_gui_context());

		if (has_main_menu()) {
			main_menu->gui.world.unhover_and_undrag(create_menu_context(main_menu->gui));
		}

		ingame_menu.world.unhover_and_undrag(create_menu_context(ingame_menu));

		on_specific_setup([](editor_setup& setup) {
			setup.unhover();
		});
	};

	static auto advance_game_gui = [&](const auto context, const auto frame_delta) {
		auto scope = measure_scope(game_thread_performance.advance_game_gui);

		game_gui.advance(context, frame_delta);
		game_gui.rebuild_layouts(context);
		game_gui.build_tree_data(context);
	};

	/* 
		MousePos is initially set to negative infinity.
	*/

	ImGui::GetIO().MousePos = { 0, 0 };

	static cached_visibility_data cached_visibility;
	static debug_details_summaries debug_summaries;

	static auto game_thread_worker = []() {
		auto prepare_next_game_frame = [&]() {
			auto frame = measure_scope(game_thread_performance.total);

			{
				/* The thread pool is always empty of tasks on the beginning of the game frame. */

				const auto requested_num_workers = config.performance.get_num_pool_workers();
				const auto current_num_workers = static_cast<int>(thread_pool.size());

				if (current_num_workers != requested_num_workers) {
					thread_pool.resize(requested_num_workers);
				}
			}

			/* Setup variables required by the lambdas */

			const auto screen_size = logic_get_screen_size();
			const auto frame_delta = frame_timer.extract_delta();
			const auto current_frame_num = current_frame.load();
			auto game_gui_mode = game_gui_mode_flag;

			auto& write_buffer = get_write_buffer();

			auto& game_gui_renderer = write_buffer.renderers.all[renderer_type::GAME_GUI];
			auto& post_game_gui_renderer = write_buffer.renderers.all[renderer_type::POST_GAME_GUI];
			auto& debug_details_renderer = write_buffer.renderers.all[renderer_type::DEBUG_DETAILS];

			/* Logic lambdas */

			auto get_current_frame_num = [&]() {
				return current_frame_num;
			};

			auto should_quit_due_to_signal = []() {
#if PLATFORM_UNIX
				if (signal_status != 0) {
					const auto sig = signal_status;

					LOG("%x received.", strsignal(sig));

					if(
						sig == SIGINT
						|| sig == SIGSTOP
						|| sig == SIGTERM
					) {
						LOG("Gracefully shutting down.");
						request_quit();
						
						return true;
					}
				}
#endif

				return false;
			};

			auto perform_input_pass = [&]() -> input_pass_result {
				/* 
					The centralized transformation of all window inputs.
					No window inputs will be acquired and/or used beyond the scope of this lambda,
					to the exception of remote packets, received by the client/server setups.
					
					This is necessary because we need some complicated interactions between multiple GUI contexts,
					primarily in deciding what events should be propagated further, down to the gameplay itself.
					It is the easiest if every possibility is considered in one place. 
					We have decided that some stronger decoupling here would benefit nobody.

					The lambda is called right away, like so: 
						result = [...](){...}().
					The result of the call, which is the collection of new game commands, will be passed further down the loop. 
				*/

				input_pass_result out;

				augs::local_entropy new_window_entropy;

				/* Generate release events if the previous frame so requested. */

				releases.append_releases(new_window_entropy, common_input_state);
				releases = {};

				concatenate(new_window_entropy, write_buffer.new_window_entropy);

				if (get_viewed_character().dead()) {
					game_gui_mode = true;
				}

				const bool in_direct_gameplay =
					!game_gui_mode
					&& has_current_setup()
					&& !ingame_menu.show
				;

				/*
					Top-level events, higher than IMGUI.
				*/
				
				{
					auto simulated_input_state = common_input_state;

					erase_if(new_window_entropy, [&](const augs::event::change e) {
						using namespace augs::event;
						using namespace augs::event::keys;

						simulated_input_state.apply(e);

						if (e.msg == message::activate) {
							if (config.content_regeneration.rescan_assets_on_window_focus) {
								streaming.request_rescan();
							}
						}

						if (e.msg == message::deactivate) {
							releases.set_all();
						}

						if (e.is_exit_message()) {
							request_quit();
							return true;
						}
						
						{
							const bool toggle_fullscreen = 
								e.was_pressed(key::F11)
#if PLATFORM_WINDOWS
								|| (e.was_pressed(key::ENTER) && common_input_state[augs::event::keys::key::LALT])
#endif
							;

							if (toggle_fullscreen) {
								bool& f = config.window.fullscreen;
								f = !f;
								return true;
							}
						}

						if (settings_gui.should_hijack_key()) {
							if (e.was_any_key_pressed()) {
								settings_gui.set_hijacked_key(e.get_key());
								return true;
							}
						}

						if (!ingame_menu.show) {
							if (visit_current_setup([&](auto& setup) {
								using T = remove_cref<decltype(setup)>;

								if constexpr(T::handles_window_input) {
									/* 
										Lets a setup fetch an input before IMGUI does,
										if for example IMGUI wants to capture keyboard input.	
									*/

									return setup.handle_input_before_imgui({
										simulated_input_state, e, window
									});
								}

								return false;
							})) {
								return true;
							}
						}

						return false;
					});
				}

				/*
					We "pause" the mouse cursor's position when we are in direct gameplay,
					so that when switching to GUI, the cursor appears exactly where it had disappeared.
					(it does not physically freeze the cursor, it just remembers the position)
				*/

				write_buffer.should_pause_cursor = in_direct_gameplay;
				
				do_imgui_pass(get_current_frame_num(), new_window_entropy, frame_delta, in_direct_gameplay);

				const auto viewing_config = get_setup_customized_config();
				out.viewing_config = viewing_config;

				configurables.apply(viewing_config);
				write_buffer.new_settings = viewing_config.window;
				write_buffer.swap_when = viewing_config.performance.swap_window_buffers_when;
				decide_on_cursor_clipping(in_direct_gameplay, viewing_config);

				releases.set_due_to_imgui(ImGui::GetIO());

				auto create_menu_context = make_create_menu_context(viewing_config);
				auto create_game_gui_context = make_create_game_gui_context(viewing_config);

				let_imgui_hijack_mouse(create_game_gui_context, create_menu_context);

				/*
					We also need inter-op between our own GUIs, 
					since we have more than just one.
				*/

				if (game_gui_mode && should_draw_game_gui() && game_gui.world.wants_to_capture_mouse(create_game_gui_context())) {
					if (current_setup) {
						if (auto* editor = std::get_if<editor_setup>(&*current_setup)) {
							editor->unhover();
						}
					}
				}

				/* Maybe the game GUI was deactivated while the button was still hovered. */

				else if (!game_gui_mode && has_current_setup()) {
					game_gui.world.unhover_and_undrag(create_game_gui_context());
				}

				/* Distribution of all the remaining input happens here. */

				for (const auto e : new_window_entropy) {
					using namespace augs::event;
					using namespace keys;
					
					/* Now is the time to actually track the input state. */
					common_input_state.apply(e);

					if (e.was_pressed(key::ESC)) {
						if (has_current_setup()) {
							if (ingame_menu.show) {
								ingame_menu.show = false;
							}
							else if (!visit_current_setup([&](auto& setup) {
								switch (setup.escape()) {
									case setup_escape_result::LAUNCH_INGAME_MENU: ingame_menu.show = true; return true;
									case setup_escape_result::JUST_FETCH: return true;
									case setup_escape_result::GO_TO_MAIN_MENU: launch_setup(launch_type::MAIN_MENU); return true;
									default: return false;
								}
							})) {
								/* Setup ignored the ESC button */
								ingame_menu.show = true;
							}

							releases.set_all();
						}

						continue;
					}

					const auto key_change = ::to_intent_change(e.get_key_change());

					const bool was_pressed = key_change == intent_change::PRESSED;
					const bool was_released = key_change == intent_change::RELEASED;
					
					if (was_pressed || was_released) {
						const auto key = e.get_key();

						if (const auto it = mapped_or_nullptr(viewing_config.app_controls, key)) {
							if (was_pressed) {
								handle_app_intent(*it);
								continue;
							}
						}
					}

					{
						auto control_main_menu = [&]() {
							if (has_main_menu() && !has_current_setup()) {
								if (main_menu->gui.show) {
									main_menu->gui.control(create_menu_context(main_menu->gui), e, do_main_menu_option);
								}

								return true;
							}

							return false;
						};

						auto control_ingame_menu = [&]() {
							if (ingame_menu.show || was_released) {
								return ingame_menu.control(create_menu_context(ingame_menu), e, do_ingame_menu_option);
							}

							return false;
						};
						
						if (was_released) {
							control_main_menu();
							control_ingame_menu();
						}
						else {
							if (control_main_menu()) {
								continue;
							}

							if (control_ingame_menu()) {
								continue;
							}

							/* Prevent e.g. panning in editor when the ingame menu is on */
							if (ingame_menu.show) {
								continue;
							}
						}
					}

					{
						if (visit_current_setup([&](auto& setup) {
							using T = remove_cref<decltype(setup)>;

							if constexpr(T::handles_window_input) {
								if (!streaming.necessary_images_in_atlas.empty()) {
									/* Viewables reloading happens later so it might not be ready yet */

									const auto& general_gui_controls = viewing_config.general_gui_controls;

									return setup.handle_input_before_game({
										general_gui_controls, streaming.necessary_images_in_atlas, common_input_state, e, window
									});
								}
							}

							return false;
						})) {
							continue;
						}
					}

					const auto viewed_character = get_viewed_character();

					if (was_released || (has_current_setup() && !ingame_menu.show)) {
						const bool direct_gameplay = viewed_character.alive() && !game_gui_mode;
						const bool game_gui_effective = viewed_character.alive() && game_gui_mode;

						if (was_released || was_pressed) {
							const auto key = e.get_key();

							if (was_released || direct_gameplay || game_gui_effective) {
								if (const auto it = mapped_or_nullptr(viewing_config.general_gui_controls, key)) {
									if (was_pressed) {
										if (handle_general_gui_intent(*it)) {
											continue;
										}
									}
								}
								if (const auto it = mapped_or_nullptr(viewing_config.inventory_gui_controls, key)) {
									if (should_draw_game_gui()) {
										const auto input_cfg = get_current_input_settings(viewing_config);
										game_gui.control_hotbar_and_action_button(get_game_gui_subject(), { *it, *key_change }, input_cfg.game_gui);

										if (was_pressed) {
											continue;
										}
									}
								}
							}

							if (const auto it = mapped_or_nullptr(viewing_config.game_controls, key)) {
								if (e.uses_mouse() && game_gui_effective) {
									/* Leave it for the game gui */
								}
								else {
									out.intents.push_back({ *it, *key_change });

									if (was_pressed) {
										continue;
									}
								}
							}
						}

						if (direct_gameplay && e.msg == message::mousemotion) {
							raw_game_motion m;
							m.motion = game_motion_type::MOVE_CROSSHAIR;
							m.offset = e.data.mouse.rel;

							out.motions.emplace_back(m);
							continue;
						}

						if (was_released || should_draw_game_gui()) {
							if (game_gui.control_gui_world(create_game_gui_context(), e)) {
								continue;
							}
						}
					}
				}

				/* 
					Notice that window inputs do not propagate
					beyond the closing of this scope.
				*/

				return out;
			};

			auto reload_needed_viewables = [&]() {
				visit_current_setup(
					[&](const auto& setup) {
						using T = remove_cref<decltype(setup)>;
						using S = viewables_loading_type;

						constexpr auto s = T::loading_strategy;

						if constexpr(s == S::LOAD_ALL || s == S::LOAD_ALL_ONLY_ONCE) {
							load_all(setup.get_viewable_defs());
						}
						else if constexpr(s == S::LOAD_ONLY_NEAR_CAMERA) {
							static_assert(always_false_v<T>, "Unimplemented");
						}
						else {
							static_assert(always_false_v<T>, "Unknown viewables loading strategy.");
						}
					}
				);
			};

			auto finalize_loading_viewables = [&](const auto& new_viewing_config) {
				streaming.finalize_load({
					audio_buffers,
					get_current_frame_num(),
					new_viewing_config.debug.measure_atlas_uploading,
					get_general_renderer(),
					get_audiovisuals().get<sound_system>()
				});
			};

			auto do_advance_setup = [&](const augs::audio_renderer* const audio_renderer, auto& with_result) {
				/* 
					Advance the current setup's logic,
					and let the audiovisual_state sample the game world 
					that it chooses via get_viewed_cosmos.

					This also advances the audiovisual state, based on the cosmos returned by the setup.
				*/

				auto scope = measure_scope(game_thread_performance.advance_setup);
				advance_current_setup(audio_renderer, frame_delta, with_result);
			};

			auto create_viewing_game_gui_context = [&](augs::renderer& chosen_renderer, const config_lua_table& viewing_config) {
				return viewing_game_gui_context {
					make_create_game_gui_context(viewing_config)(),

					{
						get_audiovisuals().get<interpolation_system>(),
						get_audiovisuals().world_hover_highlighter,
						viewing_config.hotbar,
						viewing_config.drawing,
						viewing_config.inventory_gui_controls,
						get_camera_eye(),
						get_drawer_for(chosen_renderer)
					}
				};
			};

			/* View lambdas */ 

			auto setup_the_first_fbo = [&](augs::renderer& chosen_renderer) {
				chosen_renderer.set_viewport({ vec2i{0, 0}, screen_size });

				const bool rendering_flash_afterimage = gameplay_camera.is_flash_afterimage_requested();

				if (rendering_flash_afterimage) {
					necessary_fbos.flash_afterimage->set_as_current(chosen_renderer);
				}
				else {
					augs::graphics::fbo::set_current_to_none(chosen_renderer);
				}

				chosen_renderer.clear_current_fbo();
			};

			auto draw_debug_lines = [&](augs::renderer& chosen_renderer, const config_lua_table& new_viewing_config) {
				if (DEBUG_DRAWING.enabled) {
					auto scope = measure_scope(game_thread_performance.debug_lines);

					const auto viewed_character = get_viewed_character();

					::draw_debug_lines(
						viewed_character.get_cosmos(),
						chosen_renderer,
						get_interpolation_ratio(),
						get_drawer_for(chosen_renderer).default_texture,
						new_viewing_config,
						get_camera_cone()
					);
				}
			};

			auto setup_standard_projection = [&](augs::renderer& chosen_renderer) {
				necessary_shaders.standard->set_projection(chosen_renderer, augs::orthographic_projection(vec2(screen_size)));
			};

			auto draw_game_gui = [&](augs::renderer& chosen_renderer, const config_lua_table& viewing_config) {
				auto scope = measure_scope(game_thread_performance.draw_game_gui);

				game_gui.world.draw(create_viewing_game_gui_context(chosen_renderer, viewing_config));
			};

			auto draw_mode_and_setup_custom_gui = [&](augs::renderer& chosen_renderer, const config_lua_table& new_viewing_config) {
				auto scope = measure_scope(game_thread_performance.draw_setup_custom_gui);

				const auto player_metas = visit_current_setup([&](auto& setup) {
					return setup.find_player_metas();
				});

				visit_current_setup([&](auto& setup) {
					setup.draw_custom_gui({
						all_visible,
						get_camera_cone(),
						get_blank_texture(),
						new_viewing_config,
						streaming.necessary_images_in_atlas,
						std::addressof(streaming.general_atlas),
						std::addressof(streaming.avatar_atlas),
						streaming.images_in_atlas,
						streaming.avatars_in_atlas,
						chosen_renderer,
						common_input_state.mouse.pos,
						screen_size,
						streaming.get_loaded_gui_fonts(),
						necessary_sounds,
						player_metas,
						game_gui_mode
					});

					chosen_renderer.call_and_clear_lines();
				});
			};

			auto fallback_overlay_gray_color = [&](augs::renderer& chosen_renderer) {
				streaming.general_atlas.set_as_current(chosen_renderer);

				necessary_shaders.standard->set_as_current(chosen_renderer);
				setup_standard_projection(chosen_renderer);

				get_drawer_for(chosen_renderer).color_overlay(screen_size, darkgray);
			};

			auto draw_and_choose_menu_cursor = [&](augs::renderer& chosen_renderer, auto&& create_menu_context) {
				auto scope = measure_scope(game_thread_performance.menu_gui);

				auto get_drawer = [&]() {
					return get_drawer_for(chosen_renderer);
				};

				if (has_current_setup()) {
					if (ingame_menu.show) {
						const auto context = create_menu_context(ingame_menu);
						ingame_menu.advance(context, frame_delta);

						return ingame_menu.draw({ context, get_drawer() });
					}

					return assets::necessary_image_id::INVALID;
				}
				else {
					const auto context = create_menu_context(main_menu->gui);

					main_menu->gui.advance(context, frame_delta);

#if MENU_ART
					get_drawer().aabb(streaming.necessary_images_in_atlas[assets::necessary_image_id::ART_1], ltrb(0, 0, screen_size.x, screen_size.y), white);
#endif

					const auto cursor = main_menu->gui.draw({ context, get_drawer() });

					main_menu->draw_overlays(
						get_drawer(),
						streaming.necessary_images_in_atlas,
						streaming.get_loaded_gui_fonts().gui,
						screen_size
					);

					return cursor;
				}
			};

			auto draw_non_menu_cursor = [&](augs::renderer& chosen_renderer, const config_lua_table& viewing_config, const assets::necessary_image_id menu_chosen_cursor) {
				const bool should_draw_our_cursor = viewing_config.window.is_raw_mouse_input() && !window.is_mouse_pos_paused();
				const auto cursor_drawing_pos = common_input_state.mouse.pos;

				auto get_drawer = [&]() {
					return get_drawer_for(chosen_renderer);
				};

				if (ImGui::GetIO().WantCaptureMouse) {
					if (should_draw_our_cursor) {
						get_drawer().cursor(streaming.necessary_images_in_atlas, augs::imgui::get_cursor<assets::necessary_image_id>(), cursor_drawing_pos, white);
					}
				}
				else if (menu_chosen_cursor != assets::necessary_image_id::INVALID) {
					/* We must have drawn some menu */

					if (should_draw_our_cursor) {
						get_drawer().cursor(streaming.necessary_images_in_atlas, menu_chosen_cursor, cursor_drawing_pos, white);
					}
				}
				else if (game_gui_mode && should_draw_game_gui()) {
					if (get_viewed_character()) {
						const auto& character_gui = game_gui.get_character_gui(get_game_gui_subject());

						character_gui.draw_cursor_with_tooltip(create_viewing_game_gui_context(chosen_renderer, viewing_config), should_draw_our_cursor);
					}
				}
				else {
					if (should_draw_our_cursor) {
						get_drawer().cursor(streaming.necessary_images_in_atlas, assets::necessary_image_id::GUI_CURSOR, cursor_drawing_pos, white);
					}
				}
			};

			auto make_illuminated_rendering_input = [&](augs::renderer& chosen_renderer, const config_lua_table& viewing_config) {
				thread_local std::vector<additional_highlight> highlights;
				highlights.clear();

				visit_current_setup([&](const auto& setup) {
					using T = remove_cref<decltype(setup)>;

					if constexpr(T::has_additional_highlights) {
						setup.for_each_highlight([&](auto&&... args) {
							highlights.push_back({ std::forward<decltype(args)>(args)... });
						});
					}
				});

				thread_local std::vector<special_indicator> special_indicators;
				special_indicators.clear();
				special_indicator_meta indicator_meta;

				const auto viewed_character = get_viewed_character();

				if (viewed_character) {
					visit_current_setup([&](const auto& setup) {
						setup.on_mode_with_input(
							[&](const auto&... args) {
								::gather_special_indicators(
									args..., 
									viewed_character.get_official_faction(), 
									streaming.necessary_images_in_atlas, 
									special_indicators,
									indicator_meta,
									viewed_character
								);
							}
						);
					});
				}

				auto cone = get_camera_cone();
				cone.eye.transform.pos.discard_fract();

				return illuminated_rendering_input {
					{ viewed_character, cone },
					get_queried_cone(viewing_config),
					calc_pre_step_crosshair_displacement(viewing_config),
					get_audiovisuals(),
					viewing_config.drawing,
					streaming.necessary_images_in_atlas,
					streaming.get_loaded_gui_fonts().gui,
					streaming.images_in_atlas,
					get_interpolation_ratio(),
					chosen_renderer,
					game_thread_performance,
					std::addressof(streaming.general_atlas),
					necessary_fbos,
					necessary_shaders,
					all_visible,
					viewing_config.performance,
					viewing_config.renderer,
					highlights,
					special_indicators,
					indicator_meta,
					write_buffer.particle_buffers,
					cached_visibility.light_requests,
					thread_pool
				};
			};

			auto perform_illuminated_rendering = [&](const illuminated_rendering_input& input) {
				auto scope = measure_scope(game_thread_performance.rendering_script);

				illuminated_rendering(input);
			};

			auto draw_call_imgui = [&](augs::renderer& chosen_renderer) {
				chosen_renderer.call_and_clear_triangles();

				auto scope = measure_scope(game_thread_performance.imgui);

				chosen_renderer.draw_call_imgui(
					imgui_atlas, 
					std::addressof(streaming.general_atlas), 
					std::addressof(streaming.avatar_atlas), 
					std::addressof(streaming.avatar_preview_tex)
				);
			};

			auto do_flash_afterimage = [&](augs::renderer& chosen_renderer) {
				const auto flash_mult = gameplay_camera.get_effective_flash_mult();
				const bool rendering_flash_afterimage = gameplay_camera.is_flash_afterimage_requested();

				const auto viewed_character = get_viewed_character();

				auto get_drawer = [&]() {
					return get_drawer_for(chosen_renderer);
				};

				::handle_flash_afterimage(
					chosen_renderer,
					necessary_shaders,
					necessary_fbos,
					streaming.general_atlas,
					viewed_character,
					get_drawer,
					flash_mult,
					rendering_flash_afterimage,
					screen_size
				);
			};

			auto show_developer_details = [&](augs::renderer& chosen_renderer) {
				auto scope = measure_scope(game_thread_performance.debug_details);

				const auto viewed_character = get_viewed_character();

				debug_summaries.acquire(
					viewed_character.get_cosmos(),
					game_thread_performance,
					network_performance,
					network_stats,
					server_stats,
					streaming.performance,
					streaming.general_atlas_performance,
					render_thread_performance,
					get_audiovisuals().performance
				);

				draw_debug_details(
					get_drawer_for(chosen_renderer),
					streaming.get_loaded_gui_fonts().gui,
					screen_size,
					viewed_character,
					debug_summaries
				);
			};

			/* Flow */

			if (should_quit_due_to_signal()) {
				return;
			}

			ensure_float_flags_hold();

			if (setup_requires_cursor()) {
				game_gui_mode = true;
			}

			const auto input_result = perform_input_pass();
			const auto& new_viewing_config = input_result.viewing_config;

			reload_needed_viewables();
			finalize_loading_viewables(new_viewing_config);

			auto audio_renderer = std::optional<augs::audio_renderer>();

			if (const auto audio_buffer = audio_buffers.map_write_buffer()) {
				audio_renderer.emplace(augs::audio_renderer { *audio_buffer });
			}

			do_advance_setup(audio_renderer ? std::addressof(audio_renderer.value()) : nullptr, input_result);

			auto create_menu_context = make_create_menu_context(new_viewing_config);
			auto create_game_gui_context = make_create_game_gui_context(new_viewing_config);

			/* 
				What follows is strictly view part,
				without advancement of any kind.
				
				No state is altered beyond this point,
				except for usage of graphical resources and profilers.
			*/

			/*
				Canonical rendering order of the Hypersomnia Universe:
				
				1.  Draw the cosmos in the vicinity of the viewed character.
					Both the cosmos and the character are specified by the current setup (main menu is a setup, too).
				
				2.	Draw the debug lines over the game world, if so is appropriate.
				
				3.	Draw the game GUI, if so is appropriate.
					Game GUI involves things like inventory buttons, hotbar and health bars.

				4.  Draw the mode GUI.
					Mode GUI involves things like team selection, weapon shop, round time remaining etc.

				5.	Draw either the main menu buttons, or the in-game menu overlay accessed by ESC.
					These two are almost identical, except the layouts of the first (e.g. tweened buttons) 
					may also be influenced by a playing intro.

				6.	Draw IMGUI, which is the highest priority GUI. 
					This involves settings window, developer console and the like.

				7.	Draw the GUI cursor. It may be:
						- The cursor of the IMGUI, if it wants to capture the mouse.
						- Or, the cursor of the main menu or the in-game menu overlay, if either is currently active.
						- Or, the cursor of the game gui, with maybe tooltip, with maybe dragged item's ghost, if we're in-game in GUI mode.
			*/

			setup_the_first_fbo(get_general_renderer());

			const auto viewed_character = get_viewed_character();
			const auto& viewed_cosmos = viewed_character.get_cosmos();
			const bool non_zero_cosmos = std::addressof(viewed_cosmos) != std::addressof(cosmos::zero);

			auto enqueue_visibility_jobs = [&]() {
				const auto& interp = get_audiovisuals().get<interpolation_system>();
				auto& light_requests = cached_visibility.light_requests;
				light_requests.clear();

				::for_each_vis_request(
					[&](const visibility_request& request) {
						light_requests.emplace_back(request);
					},

					viewed_cosmos,
					all_visible,

					get_audiovisuals().get<light_system>().per_entity_cache,
					interp,
					get_queried_cone(new_viewing_config).get_visible_world_rect_aabb()
				);

				const auto& fog_of_war = new_viewing_config.drawing.fog_of_war;
				const auto viewed_character_transform = viewed_character ? viewed_character.find_viewing_transform(interp) : std::optional<transformr>();

#if BUILD_STENCIL_BUFFER
				const bool fog_of_war_effective = 
					viewed_character_transform != std::nullopt 
					&& fog_of_war.is_enabled()
				;
#else
				const bool fog_of_war_effective = false;
#endif

				::enqueue_visibility_jobs(
					thread_pool,

					viewed_cosmos,
					get_general_renderer().dedicated,
					cached_visibility,

					fog_of_war_effective,
					viewed_character,
					viewed_character_transform ? *viewed_character_transform : transformr(),
					fog_of_war
				);
			};

			const auto illuminated_input = make_illuminated_rendering_input(get_general_renderer(), new_viewing_config);

			auto illuminated_rendering_job = [&]() {
				/* #1 */
				perform_illuminated_rendering(illuminated_input);
				/* #2 */
				draw_debug_lines(get_general_renderer(), new_viewing_config);

				setup_standard_projection(get_general_renderer());
			};

			auto game_gui_job = [&]() {
				advance_game_gui(create_game_gui_context(), frame_delta);
				draw_game_gui(game_gui_renderer, new_viewing_config);
			};

			auto post_game_gui_job = [&]() {
				auto& chosen_renderer = post_game_gui_renderer;

				if (non_zero_cosmos) {
					/* #4 */
					draw_mode_and_setup_custom_gui(chosen_renderer, new_viewing_config);
				}
				else {
					fallback_overlay_gray_color(chosen_renderer);
				}

				/* #5 */
				const auto menu_chosen_cursor = draw_and_choose_menu_cursor(chosen_renderer, create_menu_context);

				/* #6 */
				draw_call_imgui(chosen_renderer);

				/* #7 */
				draw_non_menu_cursor(chosen_renderer, new_viewing_config, menu_chosen_cursor);

				do_flash_afterimage(chosen_renderer);
			};

			auto show_developer_details_job = [&]() {
				show_developer_details(debug_details_renderer);
			};

			enqueue_visibility_jobs();

			if (new_viewing_config.session.show_developer_console) {
				thread_pool.enqueue(show_developer_details_job);
			}

			if (non_zero_cosmos) {
				thread_pool.enqueue(illuminated_rendering_job);

				/* #3 */
				if (should_draw_game_gui()) {
					thread_pool.enqueue(game_gui_job);
				}

				::enqueue_illuminated_rendering_jobs(thread_pool, illuminated_input);
			}

			thread_pool.enqueue(post_game_gui_job);

			thread_pool.submit();

			auto scope = measure_scope(game_thread_performance.main_help);

			thread_pool.help_until_no_tasks();
			thread_pool.wait_for_all_tasks_to_complete();
		};

		while (!should_quit) {
			auto extract_num_total_drawn_triangles = []() {
				return get_write_buffer().renderers.extract_num_total_triangles_drawn();
			};

			auto finalize_frame_and_swap = [&]() {
				for (auto& r : get_write_buffer().renderers.all) {
					r.call_and_clear_triangles();
				}

				game_thread_performance.num_triangles.measure(extract_num_total_drawn_triangles());

				buffer_swapper.wait_swap();

				{
					auto& write_buffer = get_write_buffer();

					write_buffer.renderers.next_frame();
					write_buffer.particle_buffers.clear();
				}
			};

			{
				auto scope = measure_scope(game_thread_performance.total);
				prepare_next_game_frame();
			}

			auto scope = measure_scope(game_thread_performance.main_wait);
			finalize_frame_and_swap();
		}
	};

	static auto game_thread = std::thread(game_thread_worker);

	auto audio_thread_joiner = augs::scope_guard([]() { audio_buffers.quit(); });
	auto game_thread_joiner = augs::scope_guard([]() { game_thread.join(); });

	request_quit = []() {
		get_write_buffer().should_quit = true;
		should_quit = true;
	};

	static auto game_main_thread_synced_op = []() {
		auto scope = measure_scope(game_thread_performance.synced_op);

		/* 
			IMGUI is our top GUI whose priority precedes everything else. 
			It will eat from the window input vector that is later passed to the game and other GUIs.	
		*/

		configurables.apply_main_thread(get_read_buffer().new_settings);
		configurables.sync_back_into(config);

		if (config.session.show_developer_console) {
			const auto viewed_character = get_viewed_character();

			viewed_character.get_cosmos().profiler.prepare_summary_info();
			game_thread_performance.prepare_summary_info();
			network_performance.prepare_summary_info();
			streaming.performance.prepare_summary_info();

			if (streaming.finished_generating_atlas()) {
				streaming.general_atlas_performance.prepare_summary_info();
			}

			render_thread_performance.prepare_summary_info();
			get_audiovisuals().performance.prepare_summary_info();
		}

		for (const auto& f : renderer_backend_result.imgui_lists_to_delete) {
			IM_DELETE(f);
		}
	};

	for (;;) {
		auto scope = measure_scope(render_thread_performance.fps);
		
		auto swap_window_buffers = [&]() {
			auto scope = measure_scope(render_thread_performance.swap_window_buffers);
			window.swap_buffers();

			if (!until_first_swap_measured) {
				LOG("Time until first swap: %x ms", until_first_swap.extract<std::chrono::milliseconds>());
				until_first_swap_measured = true;
			}
		};

		{
			auto& read_buffer = get_read_buffer();

			const auto swap_when = read_buffer.swap_when;

			if (swap_when == swap_buffers_moment::AFTER_HELPING_LOGIC_THREAD) {
				swap_window_buffers();
			}

			{
				auto scope = measure_scope(render_thread_performance.renderer_commands);

				renderer_backend_result.clear();

				for (auto& r : read_buffer.renderers.all) {
					renderer_backend.perform(
						renderer_backend_result,
						r.commands.data(),
						r.commands.size(),
						r.dedicated
					);
				}

				current_frame.fetch_add(1, std::memory_order_relaxed);
			}

			if (swap_when == swap_buffers_moment::AFTER_GL_COMMANDS) {
				swap_window_buffers();
			}
		}

		{
			{
				auto& read_buffer = get_read_buffer();

				auto scope = measure_scope(render_thread_performance.local_entropy);

				auto& next_entropy = read_buffer.new_window_entropy;
				next_entropy.clear();

				window.collect_entropy(next_entropy);

				read_buffer.screen_size = window.get_screen_size();
			}

			{
				auto scope = measure_scope(render_thread_performance.render_help);
				thread_pool.sleep_until_tasks_posted();
				thread_pool.help_until_no_tasks();
			}

			{
				auto scope = measure_scope(render_thread_performance.render_wait);
				buffer_swapper.swap_buffers(game_main_thread_synced_op);
			}
		}

		auto& read_buffer = get_read_buffer();

		if (window.is_active() && read_buffer.should_clip_cursor) {
			window.set_cursor_clipping(true);
			window.set_cursor_visible(false);
		}
		else {
			window.set_cursor_clipping(false);
			window.set_cursor_visible(true);
		}

		window.set_mouse_pos_paused(read_buffer.should_pause_cursor);

		if (read_buffer.should_quit) {
			break;
		}
	}

	return EXIT_SUCCESS;
}
catch (const config_read_error& err) {
	LOG("Failed to read the initial config for the game!\n%x", err.what());
	return EXIT_FAILURE;
}
catch (const augs::imgui_init_error& err) {
	LOG("Failed init imgui:\n%x", err.what());
	return EXIT_FAILURE;
}
catch (const augs::audio_error& err) {
	LOG("Failed to establish the audio context:\n%x", err.what());
	return EXIT_FAILURE;
}
catch (const augs::window_error& err) {
	LOG("Failed to create an OpenGL window:\n%x", err.what());
	return EXIT_FAILURE;
}
catch (const augs::graphics::renderer_error& err) {
	LOG("Failed to initialize the renderer: %x", err.what());
	return EXIT_FAILURE;
}
catch (const necessary_resource_loading_error& err) {
	LOG("Failed to load a resource necessary for the game to function!\n%x", err.what());
	return EXIT_FAILURE;
}
catch (const augs::lua_state_creation_error& err) {
	LOG("Failed to create a lua state for the game!\n%x", err.what());
	return EXIT_FAILURE;
}
catch (const augs::unit_test_session_error& err) {
	LOG("Unit test session failure:\n%x\ncout:%x\ncerr:%x\nclog:%x\n", 
		err.what(), err.cout_content, err.cerr_content, err.clog_content
	);

	return EXIT_FAILURE;
}