#include "http.hpp"

#include <QFile>
#include <QString>

#include <windows.h>
#include <winhttp.h>

namespace {

struct Handle {
	HINTERNET h = nullptr;
	~Handle()
	{
		if (h)
			WinHttpCloseHandle(h);
	}
};

std::wstring query_header(HINTERNET req, DWORD what)
{
	DWORD size = 0;
	WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
		return {};
	std::wstring value(size / sizeof(wchar_t), L'\0');
	if (!WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &size, WINHTTP_NO_HEADER_INDEX))
		return {};
	value.resize(size / sizeof(wchar_t));
	return value;
}

// One request without following redirects. Body goes to `out` when given, else into the response.
HttpResponse request_once(const wchar_t *method, const std::wstring &url, const std::vector<std::wstring> &headers,
			  const std::string &payload, QFile *out,
			  const std::function<void(uint64_t, uint64_t)> &progress, std::wstring &location)
{
	HttpResponse res;
	URL_COMPONENTS uc = {sizeof(uc)};
	wchar_t host[256] = {}, path[4096] = {};
	uc.lpszHostName = host;
	uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = 4096;
	uc.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
		res.error = "Bad URL";
		return res;
	}
	std::wstring target(path, uc.dwUrlPathLength);
	if (uc.lpszExtraInfo)
		target.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

	Handle session{WinHttpOpen(L"instant-clip", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
				   WINHTTP_NO_PROXY_BYPASS, 0)};
	if (!session.h) {
		res.error = "Couldn't start WinHTTP";
		return res;
	}
	WinHttpSetTimeouts(session.h, 10000, 10000, 30000, 60000);
	Handle conn{WinHttpConnect(session.h, host, uc.nPort, 0)};
	Handle req{conn.h ? WinHttpOpenRequest(conn.h, method, target.c_str(), nullptr, WINHTTP_NO_REFERER,
					       WINHTTP_DEFAULT_ACCEPT_TYPES,
					       uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
			  : nullptr};
	if (!req.h) {
		res.error = "Couldn't reach " + QString::fromWCharArray(host).toStdString();
		return res;
	}
	DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;
	WinHttpSetOption(req.h, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof(noRedirect));

	std::wstring hdrs;
	for (const std::wstring &h : headers)
		hdrs += h + L"\r\n";
	if (!WinHttpSendRequest(req.h, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(), (DWORD)-1L,
				payload.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)payload.data(), (DWORD)payload.size(),
				(DWORD)payload.size(), 0) ||
	    !WinHttpReceiveResponse(req.h, nullptr)) {
		res.error = "Network error " + std::to_string(GetLastError()) + ". Are you online?";
		return res;
	}

	DWORD status = 0, size = sizeof(status);
	WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			    &status, &size, WINHTTP_NO_HEADER_INDEX);
	res.status = (int)status;
	if (status >= 300 && status < 400) {
		location = query_header(req.h, WINHTTP_QUERY_LOCATION);
		return res;
	}

	uint64_t total = 0;
	std::wstring len = query_header(req.h, WINHTTP_QUERY_CONTENT_LENGTH);
	if (!len.empty())
		total = std::wcstoull(len.c_str(), nullptr, 10);

	uint64_t got = 0;
	std::vector<char> buf(64 * 1024);
	while (true) {
		DWORD read = 0;
		if (!WinHttpReadData(req.h, buf.data(), (DWORD)buf.size(), &read)) {
			res.error = "Download interrupted";
			return res;
		}
		if (read == 0)
			break;
		got += read;
		if (out && status == 200) {
			if (out->write(buf.data(), read) != (qint64)read) {
				res.error = "Couldn't write the download to disk";
				return res;
			}
		} else {
			res.body.append(buf.data(), read);
		}
		if (progress)
			progress(got, total);
	}
	return res;
}

} // namespace

HttpResponse http_get(std::wstring url, std::vector<std::wstring> headers, QFile *out,
		      const std::function<void(uint64_t, uint64_t)> &progress)
{
	for (int hop = 0; hop < 6; hop++) {
		std::wstring location;
		HttpResponse res = request_once(L"GET", url, headers, {}, out, progress, location);
		if (!res.error.empty() || location.empty())
			return res;
		std::erase_if(headers, [](const std::wstring &h) { return h.rfind(L"Authorization:", 0) == 0; });
		url = location;
	}
	HttpResponse res;
	res.error = "Too many redirects";
	return res;
}

HttpResponse http_post(const std::wstring &url, const std::vector<std::wstring> &headers, const std::string &body)
{
	std::wstring location;
	return request_once(L"POST", url, headers, body, nullptr, {}, location);
}
