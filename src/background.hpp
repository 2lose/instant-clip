#pragma once

#include <algorithm>
#include <thread>

#include <windows.h>

// Puts the calling thread in Windows background mode: lowest CPU scheduling plus
// very low I/O and memory priority. Clip trimming and update downloads then only use
// what OBS, its encoders and the game leave idle, instead of competing with them.
inline void enter_background_mode()
{
	SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
}

// Thread budget for FFmpeg decode/encode in precise mode: half the cores, so a
// re-encode never saturates the CPU while OBS is encoding a stream or recording.
inline int ffmpeg_thread_budget()
{
	unsigned cores = std::thread::hardware_concurrency();
	return (int)std::max(1u, cores / 2);
}
