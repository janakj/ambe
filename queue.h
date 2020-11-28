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
