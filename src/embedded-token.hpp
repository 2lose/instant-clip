#pragma once

#include <string>

// The update token embedded (encrypted) at build time, decrypted in memory.
// Empty when the build had no token.
std::string embedded_token();
