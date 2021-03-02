#include "caffeine-tracked-frames.hpp"

#define caff_ms_to_ns(ms) (ms * 1000000UL)
static int const threshold_frames_dropped_percent = 25;

CaffeineFramesTracker::CaffeineFramesTracker()
	: frames_list(new std::list<caffeine_tracked_frame>()),
	  next_check_dropped_frames(0),
	  isFramesDropped(false)
{
}

CaffeineFramesTracker::~CaffeineFramesTracker()
{
	delete frames_list;
}

void CaffeineFramesTracker::caffeine_add_frame(uint64_t timestamp, bool sent)
{
	frames_list->push_back({timestamp, sent});
	if (timestamp >= next_check_dropped_frames) {
		caffeine_remove_old_frames(timestamp);
		if (caffeine_get_drop_percent() >=
		    threshold_frames_dropped_percent) {
			isFramesDropped = true;
		} else {
			isFramesDropped = false;
		}
		next_check_dropped_frames = timestamp + caff_ms_to_ns(1000UL);
	}
}

void CaffeineFramesTracker::caffeine_remove_old_frames(uint64_t timestamp)
{
	uint64_t frame_time_lower_bound = timestamp - caff_ms_to_ns(10000UL);
	// remove old frames , Lower bound is 10 seconds
	frames_list->remove_if(
		[frame_time_lower_bound](
			const caffeine_tracked_frame &list_frame) -> bool {
			return list_frame.timestamp < frame_time_lower_bound;
		});
}

uint32_t CaffeineFramesTracker::caffeine_get_drop_percent()
{
	float frames_dropped = 0.0f;
	float total_frames = 0.0f;
	for (auto f = frames_list->begin(); f != frames_list->end(); f++) {
		if (!f->sent) {
			frames_dropped = frames_dropped + 1.0f;
		}
		total_frames = total_frames + 1.0f;
	}
	return (uint32_t)((frames_dropped / total_frames) * 100.0f);
}

void CaffeineFramesTracker::caffeine_set_next_check_dropped_frames(
	uint64_t next_check)
{
	next_check_dropped_frames = next_check;
}

bool CaffeineFramesTracker::caffeine_did_frames_drop()
{
	return isFramesDropped;
}
