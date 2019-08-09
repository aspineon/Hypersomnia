#pragma once
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "augs/audio/audio_command.h"
#include "augs/audio/audio_backend.h"

static constexpr int num_audio_buffers_v = 50;

namespace augs {
	class audio_command_buffers {
		augs::audio_backend backend;
		std::optional<std::thread> audio_thread;

		std::condition_variable for_new_buffers;
		std::condition_variable for_completion;
		std::mutex queue_mutex;

		bool should_quit = false;
		int read_index = 0;
		int write_index = 0;

		std::array<audio_command_buffer, num_audio_buffers_v> buffers;

		int next_to(const int idx) const {
			if (idx == num_audio_buffers_v - 1) {
				return 0;
			}

			return idx + 1;
		}

		int prev_to(const int idx) const {
			if (idx == 0) {
				return num_audio_buffers_v - 1;
			}

			return idx - 1;
		}

		auto lock_queue() {
			return std::unique_lock<std::mutex>(queue_mutex);
		}

		auto make_worker_lambda() {
			return [this]() {
				for (;;) {
					const augs::audio_command_buffer* cmds; 

					{
						auto lk = lock_queue();
						for_new_buffers.wait(lk, [&]{ return should_quit || has_tasks(); });

						if (should_quit && !has_tasks()) {
							return;
						}

						cmds = std::addressof(get_read_buffer());
					}

					backend.perform(
						cmds->data(),
						cmds->size()
					);

					report_completion();
				}
			};
		}

		void report_completion() {
			auto lk = lock_queue();
			read_index = next_to(read_index);

			if (has_finished()) {
				for_completion.notify_all();
			}
		}

		bool is_full() const {
			return write_index == prev_to(read_index);
		}

		bool has_finished() const {
			return read_index == write_index;
		}

		bool has_tasks() const {
			return !has_finished();
		}

		const audio_command_buffer& get_read_buffer() const {
			return buffers[read_index];
		}

		audio_command_buffers(audio_command_buffers&&) = delete;
		audio_command_buffers(const audio_command_buffers&) = delete;

		audio_command_buffers& operator=(audio_command_buffers&&) = delete;
		audio_command_buffers& operator=(const audio_command_buffers&) = delete;
		
		void request_quit() {
			{
				auto lk = lock_queue();
				should_quit = true;
			}

			for_new_buffers.notify_all();
		}

	public:
		audio_command_buffers() {
			audio_thread.emplace(make_worker_lambda());
		}

		void quit() {
			request_quit();
			audio_thread->join(); 
			stop_all_sources();
		}

		audio_command_buffer* map_write_buffer() {
			auto lk = lock_queue();

			if (is_full()) {
				return nullptr;
			}

			return buffers.data() + write_index;
		}

		auto submit_write_buffer() {
			auto lk = lock_queue();

			if (buffers[write_index].empty()) {
				return;
			}

			write_index = next_to(write_index);
			buffers[write_index].clear();

			for_new_buffers.notify_all();
		}

		void finish() {
			auto lk = lock_queue();
			for_completion.wait(lk, [&]() { return has_finished(); });
		}

		template <class F>
		void stop_sources_if(F&& pred) {
			backend.stop_sources_if(std::forward<F>(pred));
		}

		void stop_all_sources() {
			backend.stop_sources_if([&](auto&&...) { return true; });
		}
	};
}
