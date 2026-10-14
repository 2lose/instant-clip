#pragma once

#include <string>

// Values embedded (encrypted) at build time by scripts/embed-secrets.ps1 and
// decrypted in memory on use. Empty when the build didn't have that value.
enum class Secret { UpdateToken, FeedbackUrl, FeedbackKey };

std::string embedded_secret(Secret which);
