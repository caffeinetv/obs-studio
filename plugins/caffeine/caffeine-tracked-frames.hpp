#ifndef CAFFEINE_TRACKED_FRAMES_H
#define CAFFEINE_TRACKED_FRAMES_H

#include <list>
#include <obs-data.h>

struct caffeine_tracked_frame {
	uint64_t timestamp;
	bool sent;
};

class CaffeineFramesTracker {
public:
	CaffeineFramesTracker(obs_data_t *data);
	~CaffeineFramesTracker();
	void caffeine_add_frame(uint64_t timestamp, bool sent);
	void caffeine_set_next_check_dropped_frames(uint64_t next_check);

private:
	std::list<caffeine_tracked_frame> *frames_list;
	uint64_t next_check_dropped_frames;
	void caffeine_remove_old_frames(uint64_t timestamp);
	uint32_t caffeine_get_drop_percent();
	obs_data_t *data;
};
#endif