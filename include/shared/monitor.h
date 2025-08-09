#pragma once

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <type_traits>

namespace Threading {
	template<typename T>
	class Monitor {
	public:
		Monitor() {
			_timeout.store(100, std::memory_order_relaxed);
		}

		std::optional<T> Get(bool* timedout) {
			std::unique_lock<std::timed_mutex> lck(_mutex, std::defer_lock);
			bool locked = lck.try_lock_for(std::chrono::milliseconds(_timeout.load(std::memory_order_acquire)));
			if (!locked || _queue.empty()) {
				if (timedout) {
					*timedout = !locked;
				}
				return std::nullopt;
			}

			T result = std::move(_queue.front());
			_queue.pop();

			return result;
		}

		template<typename T>
		void Push(T&& t) {
			_mutex.lock();
			_queue.push(std::forward<T>(t));
			_mutex.unlock();
		}

		void SetTimeout(uint32_t timeout) {
			_timeout.store(timeout, std::memory_order_release);
		}

	private:
		std::atomic<uint32_t> _timeout;
		std::queue<T> _queue;
		std::timed_mutex _mutex;
		std::condition_variable _cv;
	};

	template<typename T, typename U>
	using MonitoredFuture = std::tuple<std::future<T>, Monitor<U>*>;
}