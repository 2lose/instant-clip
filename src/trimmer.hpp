#pragma once

#include <functional>
#include <string>

namespace trimmer {

enum class Mode { Fast, Precise };

struct Request {
	std::string input;  // UTF-8 path of the full-length replay
	std::string output; // UTF-8 path of the clip; container picked from `format`
	std::string format; // "mp4", "mkv" or "mov"
	double length = 30.0;    // seconds to keep
	double endOffset = 0.0;  // seconds to drop from the end (time between hotkey press and buffer save)
	Mode mode = Mode::Fast;
	std::function<void(double)> progress; // 0..1, called from the worker thread
};

struct Result {
	bool ok = false;
	std::string error;
	double duration = 0.0;     // seconds actually written
	double available = 0.0;    // seconds the replay held
	bool bufferShort = false;  // replay was shorter than requested
	bool fellBack = false;     // precise mode wasn't possible, used fast mode
	std::string encoder;       // encoder used in precise mode
};

// Cuts [end - endOffset - length, end - endOffset] out of `input` into `output`.
// Blocking; run on a worker thread.
Result trim(const Request &req);

} // namespace trimmer
