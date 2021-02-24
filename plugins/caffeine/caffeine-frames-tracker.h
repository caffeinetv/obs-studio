#pragma once

#include <obs-module.h>
#include "caffeine-queue.h"

typedef struct caffeine_frames_tracker_ {
	caffeine_queue data;
} caffeine_frames_tracker;

/** Function to initialize struct */
void caffeine_frames_tracker_init(caffeine_frames_tracker *tracker);

/** Function to insert element to queue within 10 sec frame*/
void set_frames(caffeine_frames_tracker *tracker, uint64_t timestamp_ns);

/** Function to get all frames tracked within 10 sec interval*/
int get_tracked_frames(caffeine_frames_tracker *tracker, uint64_t timestamp_ns);

/** Function to cleanup frames which are out of 10 sec interval*/
void cleanup(caffeine_frames_tracker *tracker, uint64_t timestamp_ns);
