// Round-trip check for the embedded update token: build with INSTANT_CLIP_UPDATE_TOKEN set
// to a dummy value, then run with the same value. Prints only match/mismatch, never the token.
#include "embedded-token.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
	const char *expected = std::getenv("INSTANT_CLIP_UPDATE_TOKEN");
	std::string got = embedded_token();
	if (!expected) {
		std::printf("embedded token present: %s (%zu chars)\n", got.empty() ? "no" : "yes", got.size());
		return 0;
	}
	bool ok = got == expected;
	std::printf("round trip: %s\n", ok ? "match" : "MISMATCH");
	return ok ? 0 : 1;
}
