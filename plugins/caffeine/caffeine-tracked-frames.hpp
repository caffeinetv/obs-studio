#ifndef CAFFEINE_TRACKED_FRAMES_H
#define CAFFEINE_TRACKED_FRAMES_H

#include <list>
struct caffeine_tracked_frame {
	uint64_t timestamp;
	bool sent;
};

class CaffeineFramesTracker {
public:
	CaffeineFramesTracker();
	~CaffeineFramesTracker();
	void caffeine_add_frame(uint64_t timestamp, bool sent);
	void caffeine_set_next_check_dropped_frames(uint64_t next_check);
	bool caffeine_did_frames_drop();

private:
	std::list<caffeine_tracked_frame> *frames_list;
	uint64_t next_check_dropped_frames;
	bool isFramesDropped;
	void caffeine_remove_old_frames(uint64_t timestamp);
	uint32_t caffeine_get_drop_percent();
};
#endif