// simple64X: version flag and GitHub update check (Windows).
//
// The check is one HTTPS request to GitHub's "latest release" API with no
// library to ship. It runs off the GUI thread and any failure - offline, a
// firewall, GitHub down - is just an empty answer.
#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <string>

#pragma comment(lib, "winhttp.lib")

// This fork's own version, shown by -v and compared with the newest release.
#define SIMPLE64X_VERSION "1.0.0"

namespace simple64Update
{

// Fixed here, not taken from GitHub's reply, so "Open the download page" can
// only ever open simple64X's own release page.
inline const char* releasesPage() { return "https://github.com/GamesExObscura/simple64X/releases/latest"; }

// "v1.2.3" or "1.2.3" as three numbers. Anything missing counts as 0.
inline std::array<int, 3> parseVersion(const std::string& text)
{
	std::array<int, 3> parts{};
	size_t at = 0;
	if (at < text.size() && (text[at] == 'v' || text[at] == 'V'))
	{
		++at;
	}
	for (auto& part : parts)
	{
		const size_t first = at;
		while (at < text.size() && text[at] >= '0' && text[at] <= '9' && at - first < 6)
		{
			part = part * 10 + (text[at] - '0');
			++at;
		}
		if (at == first || at >= text.size() || text[at] != '.')
		{
			break;
		}
		++at;
	}
	return parts;
}

// simple64X.exe -v or --version. Front ends such as LaunchBox run it to find out
// which version is installed, so it prints one line and leaves without a
// window, an audio device or a single file touched.
inline bool isVersionFlag(const char* argument)
{
	return _stricmp(argument, "-v") == 0 || _stricmp(argument, "--version") == 0;
}

// simple64X is a windowed program, so normally there is nowhere to print. A caller
// that connects a pipe or a file (which is what a redirected front end is) is
// used first. Failing that, the line goes to the console it was started from.
inline void printVersion()
{
	const std::string line = std::string("simple64X ") + SIMPLE64X_VERSION + "\r\n";
	DWORD written = 0;
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (out != nullptr && out != INVALID_HANDLE_VALUE)
	{
		const DWORD type = GetFileType(out);
		if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK)
		{
			WriteFile(out, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
			return;
		}
	}
	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		HANDLE console = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		                             nullptr, OPEN_EXISTING, 0, nullptr);
		if (console != INVALID_HANDLE_VALUE)
		{
			WriteFile(console, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
			CloseHandle(console);
		}
		FreeConsole();
	}
}

// The newest release's tag from GitHub, or empty on any failure. Drafts and
// pre-releases are never "latest", so they never prompt.
inline std::string fetchLatestTag(const wchar_t* agent, const wchar_t* path)
{
	std::string tag;
	HINTERNET session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == nullptr)
	{
		return tag;
	}
	WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
	HINTERNET connection = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection == nullptr
	                        ? nullptr
	                        : WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
	                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if (request != nullptr &&
	    WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1),
	                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
	    WinHttpReceiveResponse(request, nullptr) &&
	    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) &&
	    status == 200)
	{
		// The reply is a few kilobytes. The cap only stops a broken proxy
		// from feeding this forever.
		constexpr size_t kMaxReply = 1 << 20;
		std::string body;
		DWORD available = 0;
		while (WinHttpQueryDataAvailable(request, &available) && available > 0 &&
		       body.size() + available <= kMaxReply)
		{
			const size_t oldSize = body.size();
			body.resize(oldSize + available);
			DWORD read = 0;
			if (!WinHttpReadData(request, &body[oldSize], available, &read) || read == 0)
			{
				body.resize(oldSize);
				break;
			}
			body.resize(oldSize + read);
		}
		// tag_name comes before the release notes in the reply, so the first
		// match is the field and never text quoted in the notes.
		const auto key = body.find("\"tag_name\"");
		const auto open = key == std::string::npos ? key : body.find('"', key + 10);
		const auto close = open == std::string::npos ? open : body.find('"', open + 1);
		if (close != std::string::npos && close - open - 1 <= 32)
		{
			const auto value = body.substr(open + 1, close - open - 1);
			const bool plain = std::all_of(value.begin(), value.end(), [](char c) {
				return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				       c == '.' || c == '-' || c == '_';
			});
			if (plain)
			{
				tag = value;
			}
		}
	}
	if (request != nullptr)
	{
		WinHttpCloseHandle(request);
	}
	if (connection != nullptr)
	{
		WinHttpCloseHandle(connection);
	}
	WinHttpCloseHandle(session);
	return tag;
}

inline std::string fetchLatestTag64()
{
	return fetchLatestTag(L"simple64X/" L"" SIMPLE64X_VERSION, L"/repos/GamesExObscura/simple64X/releases/latest");
}

} // namespace simple64Update

#endif // _WIN32
