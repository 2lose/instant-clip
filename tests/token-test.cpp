// Round-trip check for the embedded secrets: build with the INSTANT_CLIP_* env vars set
// to dummy values, then run with the same values. Prints only match/mismatch, never values.
#include "embedded-token.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
	struct {
		Secret which;
		const char *env;
	} checks[] = {{Secret::UpdateToken, "INSTANT_CLIP_UPDATE_TOKEN"},
		      {Secret::FeedbackUrl, "INSTANT_CLIP_FEEDBACK_URL"},
		      {Secret::FeedbackKey, "INSTANT_CLIP_FEEDBACK_KEY"}};
	int failures = 0;
	for (auto &c : checks) {
		std::string got = embedded_secret(c.which);
		const char *expected = std::getenv(c.env);
		if (!expected) {
			std::printf("%s: embedded=%s (%zu chars)\n", c.env, got.empty() ? "no" : "yes", got.size());
			continue;
		}
		bool ok = got == expected;
		failures += !ok;
		std::printf("%s: %s\n", c.env, ok ? "match" : "MISMATCH");
	}
	return failures;
}
