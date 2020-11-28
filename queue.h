/*
 * A library for working with DVSI's AMBE vocoder chips
 *
 * Copyright (C) 2019-2020 Internet Real-Time Lab, Columbia University
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;

class SyncQueueEmpty : public exception {};

/*
 * A thread-safe queue implementation synchronized via a condition
 * variable. Provides a push method which pushes an element at the end
 * of the queue, and a pop operation which pops an element from the
 * front of the queue. The pop operation blocks if the queue is empty.
 */
template <class T>
class SyncQueue {
public:
	SyncQueue() : queue(), mutex(), notifier() {};

	void push(T value) {
		lock_guard<std::mutex> lock(mutex);
		queue.push(move(value));
		notifier.notify_one();
	}

	T pop(bool block=true) {
		unique_lock<std::mutex> lock(mutex);

		if (!block && queue.empty()) throw SyncQueueEmpty();

		while(queue.empty()) notifier.wait(lock);

		T v = move(queue.front());
		queue.pop();
		return v;
	}

	bool empty() const {
		lock_guard<std::mutex> lock(mutex);
		return queue.empty();
	}

	size_t size() const {
		lock_guard<std::mutex> lock(mutex);
		return queue.size();
	}

private:
	std::queue<T> queue;
	mutable std::mutex mutex;
	condition_variable notifier;
};
