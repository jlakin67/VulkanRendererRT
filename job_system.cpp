#include "job_system.h"
#include <iostream>

 void threadFunc(JobQueue* jobQueue) {
	Job job;
	while (true) {
		job = jobQueue->popJob();
		job.func(job.jobArgs);
		job.counter->decrement();
	}
 }
