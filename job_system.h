#pragma once
#include "config.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <emmintrin.h>
#include <array>
#include <functional>
#include <cassert>
#include <algorithm>
#include <vector>
#include <sstream>

class JobCounter {
public:
	JobCounter() { count = 0; }
	void wait() {
		while (isBusy()) {
			_mm_pause();
		}
	}
	bool isBusy() {
		return count.load() > 0;
	}
	void increment() {
		count++; assert(count.load() >= 0);
	}
	void decrement() {
		count--; assert(count.load() >= 0);
	}
	void fetchAdd(int n) {
		count.fetch_add(n); assert(count.load() >= 0);
	}
private:
	std::atomic<int> count;
};

struct Job {
	std::function<void(void*)> func;
	void* jobArgs = nullptr;
	JobCounter* counter = nullptr; //can have separate counters per job so you can wait on a batch of jobs
								   //or a specific type of job instead of waiting for the whole queue to be empty
};

class JobQueue {
public:
	JobQueue() {
		availableJobs = 0;
		read_pos = 0;
		write_pos = 0;
	}

	bool pushJob(Job job) {
		std::unique_lock<std::mutex> lock(queue_lock);
		if (availableJobs == MAX_JOBS) {
			lock.unlock();
			return false;
		}
		jobQueue[write_pos] = job;
		write_pos = (write_pos + 1) % MAX_JOBS;
		job.counter->increment();
		availableJobs++;
		lock.unlock();
		jobAvailableCond.notify_one();
		assert(availableJobs >= 0);
		return true;
	}

	//returns number of jobs successfully pushed
	int pushJobs(size_t numJobs, Job* jobs) {
		std::unique_lock<std::mutex> lock(queue_lock);
		int jobsPushed = 0;
		for (size_t i = 0; i < numJobs; i++) {
			if (availableJobs == MAX_JOBS) {
				lock.unlock();
				for (int j = 0; j < jobsPushed; j++) {
					jobAvailableCond.notify_one();
				}
				return jobsPushed;
			}
			jobQueue[write_pos] = jobs[i];
			write_pos = (write_pos + 1) % MAX_JOBS;
			jobs[i].counter->increment();
			availableJobs++;
			jobsPushed++;
		}
		lock.unlock();
		jobAvailableCond.notify_all();
		assert(availableJobs >= 0);
		return jobsPushed;
	}

	Job popJob() {
		std::unique_lock<std::mutex> lock(queue_lock);
		while (availableJobs == 0) {
			jobAvailableCond.wait(lock);
		}
		auto job = jobQueue[read_pos];
		availableJobs--;
		read_pos = (read_pos + 1) % MAX_JOBS;
		lock.unlock();
		assert(availableJobs >= 0);
		return job;
	}

private:
	int availableJobs;
	std::mutex queue_lock;
	std::condition_variable jobAvailableCond;
	std::array<Job, MAX_JOBS> jobQueue;
	size_t write_pos, read_pos;
};

void threadFunc(JobQueue* jobQueue);

class JobManager {
public:
	static int getMaxPossibleThreads() {
		int numCores = std::thread::hardware_concurrency();
		return std::max(0, numCores - 1); //exclude main thread
	}
	void initialize(int numThreads) {
		int threadCount = std::min(getMaxPossibleThreads(), numThreads);
		for (int i = 1; i <= threadCount; i++) {
			std::thread worker(threadFunc, &jobQueue);
			worker.detach();
			threads.push_back(std::move(worker));
		}

	}
	JobQueue jobQueue;
private:
	std::vector<std::thread> threads;
};

class IOThread {
public:
	void initialize() {
		std::thread worker(threadFunc, &jobQueue);
		workerThread = std::move(worker);
		workerThread.detach();
	}
	JobQueue jobQueue;
private:
	std::thread workerThread;
};