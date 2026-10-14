#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class QFile;

// Minimal blocking WinHTTP client. Call from a worker thread, never the UI thread.

struct HttpResponse {
	int status = 0;
	std::string body;
	std::string error; // transport error; empty when a response arrived
};

// GET, following redirects manually so an Authorization header never follows a
// redirect to another host (GitHub's signed storage URLs reject it). With `out`,
// a 200 body streams into the file instead of `body`.
HttpResponse http_get(std::wstring url, std::vector<std::wstring> headers, QFile *out = nullptr,
		      const std::function<void(uint64_t, uint64_t)> &progress = {});

// POST a body (no redirects).
HttpResponse http_post(const std::wstring &url, const std::vector<std::wstring> &headers, const std::string &body);
