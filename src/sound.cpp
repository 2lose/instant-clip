#include "sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <windows.h>
#include <mmsystem.h>

namespace {

// Synthesised once: two soft sine notes (E6 then A6) with quick decay, 16-bit mono WAV.
const std::vector<uint8_t> &chime()
{
	static const std::vector<uint8_t> wav = [] {
		constexpr int rate = 44100;
		constexpr double pi = 3.14159265358979323846;
		struct Note {
			double freq, start, len;
		};
		const Note notes[] = {{1318.5, 0.0, 0.16}, {1760.0, 0.09, 0.28}};
		const int samples = (int)(rate * 0.40);

		std::vector<int16_t> pcm(samples);
		for (int i = 0; i < samples; i++) {
			double t = (double)i / rate, v = 0.0;
			for (const Note &n : notes) {
				double local = t - n.start;
				if (local < 0 || local > n.len)
					continue;
				double attack = std::min(local / 0.006, 1.0);
				double decay = std::exp(-local * 14.0);
				v += std::sin(2 * pi * n.freq * local) * attack * decay;
			}
			pcm[i] = (int16_t)(v * 0.22 * 32767);
		}

		auto put32 = [](std::vector<uint8_t> &b, uint32_t x) {
			for (int k = 0; k < 4; k++)
				b.push_back((uint8_t)(x >> (8 * k)));
		};
		auto put16 = [](std::vector<uint8_t> &b, uint16_t x) {
			b.push_back((uint8_t)x);
			b.push_back((uint8_t)(x >> 8));
		};
		auto tag = [](std::vector<uint8_t> &b, const char *s) { b.insert(b.end(), s, s + 4); };

		uint32_t dataBytes = (uint32_t)(pcm.size() * 2);
		std::vector<uint8_t> b;
		tag(b, "RIFF");
		put32(b, 36 + dataBytes);
		tag(b, "WAVE");
		tag(b, "fmt ");
		put32(b, 16);
		put16(b, 1); // PCM
		put16(b, 1); // mono
		put32(b, rate);
		put32(b, rate * 2);
		put16(b, 2);
		put16(b, 16);
		tag(b, "data");
		put32(b, dataBytes);
		size_t at = b.size();
		b.resize(at + dataBytes);
		std::memcpy(b.data() + at, pcm.data(), dataBytes);
		return b;
	}();
	return wav;
}

} // namespace

void play_save_sound()
{
	PlaySoundW(reinterpret_cast<LPCWSTR>(chime().data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
