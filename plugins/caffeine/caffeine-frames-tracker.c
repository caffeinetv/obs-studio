#pragma once

#include "caffeine-frames-tracker.h"
#define NANOSECONDS 1000000000ull

void caffeine_frames_tracker_init(caffeine_frames_tracker *tracker)
{
	memset(tracker, 0, sizeof(caffeine_frames_tracker));
	caffeine_queue_init(&tracker->data);
}

void set_frames(caffeine_frames_tracker *tracker, uint64_t timestamp_ns)
{
	// Push the data to queue
	if (!caffeine_queue_is_full(&tracker->data)) {
		caffeine_enqueue(&tracker->data, timestamp_ns);
	}
}

int get_tracked_frames(caffeine_frames_tracker *tracker, uint64_t timestamp_ns)
{
	cleanup(tracker, timestamp_ns);
	// Dropped frames
	return caffeine_queue_size(&tracker->data);
}

void cleanup(caffeine_frames_tracker *tracker, uint64_t timestamp_ns)
{
	while (!caffeine_queue_is_empty(&tracker->data) &&
	       (caffeine_queue_get_first_element(&tracker->data) <
		(timestamp_ns - 10 * NANOSECONDS))) {
		caffeine_dequeue(&tracker->data);
	}
}
