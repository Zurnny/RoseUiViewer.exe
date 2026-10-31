#include "Updater.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

#ifdef _WIN32
	#include <windows.h>
	#include <winhttp.h>
#endif

namespace fs = std::filesystem;

static const char* kRepo = "Zurnny/RoseUiViewer.exe";
static const char* kAssetName = "RoseUiViewer.exe";

// ---------------------------------------------------------------- SHA-256 (small, self-contained)

namespace {

struct Sha256 {
	uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	uint8_t buf[64];
	size_t len = 0;
	uint64_t total = 0;

	static uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
	void Block(const uint8_t* p)
	{
		static const uint32_t k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
			0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
			0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351,
			0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
			0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3,
			0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
		uint32_t w[64];
		for (int i = 0; i < 16; i++)
			w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
		for (int i = 16; i < 64; i++)
		{
			uint32_t s0 = R(w[i - 15], 7) ^ R(w[i - 15], 18) ^ (w[i - 15] >> 3);
			uint32_t s1 = R(w[i - 2], 17) ^ R(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for (int i = 0; i < 64; i++)
		{
			uint32_t t1 = hh + (R(e, 6) ^ R(e, 11) ^ R(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			uint32_t t2 = (R(a, 2) ^ R(a, 13) ^ R(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
		}
		h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
	}
	void Add(const void* data, size_t n)
	{
		const uint8_t* p = (const uint8_t*)data;
		total += n;
		while (n--)
		{
			buf[len++] = *p++;
			if (len == 64)
			{
				Block(buf);
				len = 0;
			}
		}
	}
	std::string Hex()
	{
		uint64_t bits = total * 8;
		uint8_t pad = 0x80;
		Add(&pad, 1);
		uint8_t zero = 0;
		while (len != 56)
			Add(&zero, 1);
		uint8_t lenb[8];
		for (int i = 0; i < 8; i++)
			lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
		Add(lenb, 8);
		char out[65];
		for (int i = 0; i < 8; i++)
			snprintf(out + i * 8, 9, "%08x", h[i]);
		return std::string(out, 64);
	}
};

std::string Sha256Hex(const std::string& data)
{
	Sha256 s;
	s.Add(data.data(), data.size());
	return s.Hex();
}

std::string LowerTrim(std::string s)
{
	for (auto& c : s)
		c = (char)std::tolower((unsigned char)c);
	while (!s.empty() && std::isspace((unsigned char)s.back()))
		s.pop_back();
	size_t a = 0;
	while (a < s.size() && std::isspace((unsigned char)s[a]))
		a++;
	return s.substr(a);
}

#ifdef _WIN32
std::wstring Widen(const std::string& s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w(n, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
	return w;
}

// HTTPS GET (follows redirects). progress: optional 0..1 callback target.
bool HttpGet(const std::string& url, std::string& out, std::string& error, std::atomic<float>* progress = nullptr)
{
	out.clear();
	URL_COMPONENTS uc = {};
	uc.dwStructSize = sizeof(uc);
	wchar_t host[256], path[2048];
	uc.lpszHostName = host;
	uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = 2048;
	wchar_t extra[2048];
	uc.lpszExtraInfo = extra;
	uc.dwExtraInfoLength = 2048;
	std::wstring wurl = Widen(url);
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
	{
		error = "bad address";
		return false;
	}
	std::wstring full_path = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
	HINTERNET s = WinHttpOpen(L"RoseUiViewer-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s)
		s = WinHttpOpen(L"RoseUiViewer-Updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s)
	{
		error = "no internet access";
		return false;
	}
	bool ok = false;
	HINTERNET c = WinHttpConnect(s, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
	HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", full_path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
						  uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
					: nullptr;
	if (r)
	{
		const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
		if (WinHttpSendRequest(r, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr))
		{
			DWORD status = 0, sz = sizeof(status);
			WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
			DWORD length = 0;
			sz = sizeof(length);
			WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &length, &sz, WINHTTP_NO_HEADER_INDEX);
			if (status == 200)
			{
				ok = true;
				for (;;)
				{
					DWORD avail = 0;
					if (!WinHttpQueryDataAvailable(r, &avail))
					{
						ok = false;
						error = "download interrupted";
						break;
					}
					if (avail == 0)
						break;
					std::string chunk(avail, '\0');
					DWORD read = 0;
					if (!WinHttpReadData(r, chunk.data(), avail, &read))
					{
						ok = false;
						error = "download interrupted";
						break;
					}
					out.append(chunk.data(), read);
					if (progress && length)
						*progress = (float)out.size() / (float)length;
				}
			}
			else
				error = "server answered " + std::to_string(status);
		}
		else
			error = "couldn't reach GitHub";
	}
	else
		error = "couldn't reach GitHub";
	if (r)
		WinHttpCloseHandle(r);
	if (c)
		WinHttpCloseHandle(c);
	WinHttpCloseHandle(s);
	return ok;
}
#endif

} // namespace

// ---------------------------------------------------------------- Updater

Updater::~Updater()
{
	if (worker.joinable())
		worker.join();
}

bool Updater::IsNewer(const std::string& a, const std::string& b)
{
	auto parts = [](const std::string& v) {
		std::vector<long> p;
		std::string s = v;
		if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
			s = s.substr(1);
		std::stringstream ss(s);
		std::string item;
		while (std::getline(ss, item, '.'))
		{
			try
			{
				p.push_back(std::stol(item));
			} catch (...)
			{
				p.push_back(0);
			}
		}
		return p;
	};
	auto pa = parts(a), pb = parts(b);
	size_t n = std::max(pa.size(), pb.size());
	pa.resize(n, 0);
	pb.resize(n, 0);
	return pa > pb;
}

std::string Updater::LatestVersion()
{
	std::lock_guard<std::mutex> l(mutex);
	return latest;
}

std::string Updater::Message()
{
	std::lock_guard<std::mutex> l(mutex);
	return message;
}

void Updater::SetFailed(const std::string& why)
{
	{
		std::lock_guard<std::mutex> l(mutex);
		message = why;
	}
	state = State::Failed;
}

void Updater::Start(const fs::path& exe_in, const std::string& version_in, bool check_now)
{
	exe = exe_in;
	version = version_in;
#ifdef _WIN32
	// Leftover from the last update.
	std::error_code ec;
	fs::remove(exe.parent_path() / "RoseUiViewer.old.exe", ec);
	fs::remove(exe.parent_path() / "RoseUiViewer.new.exe", ec);
	if (version == "dev")
	{
		state = State::Unsupported;
		std::lock_guard<std::mutex> l(mutex);
		message = "This is a local build; updates come with the GitHub builds.";
		return;
	}
	if (check_now)
		CheckNow();
#else
	(void)check_now;
	state = State::Unsupported;
	message = "Automatic updates are for the Windows version.";
#endif
}

void Updater::CheckNow()
{
	const State s = state;
	if (s == State::Checking || s == State::Downloading || s == State::Ready || s == State::Unsupported)
		return;
	if (worker.joinable())
		worker.join();
	state = State::Checking;
	worker = std::thread([this] { Run(); });
}

void Updater::Run()
{
#ifdef _WIN32
	std::string body, error;
	if (!HttpGet(std::string("https://api.github.com/repos/") + kRepo + "/releases/latest", body, error))
	{
		SetFailed("Couldn't check for updates (" + error + ").");
		return;
	}
	nlohmann::json rel;
	try
	{
		rel = nlohmann::json::parse(body);
	} catch (...)
	{
		SetFailed("Couldn't read the update information.");
		return;
	}
	const std::string tag = rel.value("tag_name", "");
	{
		std::lock_guard<std::mutex> l(mutex);
		latest = tag;
	}
	if (tag.empty() || !IsNewer(tag, version))
	{
		state = State::UpToDate;
		return;
	}

	// Find the exe and its checksum.
	std::string exe_url, sum_url, digest;
	long long size = -1;
	if (rel.contains("assets"))
		for (auto& a : rel["assets"])
		{
			const std::string name = a.value("name", "");
			if (name == kAssetName)
			{
				exe_url = a.value("browser_download_url", "");
				size = a.value("size", -1LL);
				digest = a.value("digest", "");
			}
			else if (name == std::string(kAssetName) + ".sha256")
				sum_url = a.value("browser_download_url", "");
		}
	if (exe_url.empty())
	{
		SetFailed("The newest release (" + tag + ") has no RoseUiViewer.exe to download.");
		return;
	}
	std::string expected;
	if (!sum_url.empty())
	{
		std::string sum;
		if (HttpGet(sum_url, sum, error))
			expected = LowerTrim(sum.substr(0, sum.find_first_of(" \t\r\n")));
	}
	if (expected.empty() && digest.rfind("sha256:", 0) == 0)
		expected = LowerTrim(digest.substr(7));
	if (expected.size() != 64)
	{
		SetFailed("The newest release has no checksum, so it wasn't installed.");
		return;
	}

	state = State::Downloading;
	progress = 0;
	std::string data;
	if (!HttpGet(exe_url, data, error, &progress))
	{
		SetFailed("Couldn't download the update (" + error + ").");
		return;
	}
	if ((size >= 0 && (long long)data.size() != size) || data.size() < 2 || data[0] != 'M' || data[1] != 'Z' || Sha256Hex(data) != expected)
	{
		SetFailed("The downloaded update didn't check out, so it wasn't installed.");
		return;
	}

	// Swap: running exe -> .old, new -> RoseUiViewer.exe.
	const fs::path dir = exe.parent_path();
	const fs::path fresh = dir / "RoseUiViewer.new.exe", old = dir / "RoseUiViewer.old.exe";
	{
		std::ofstream f(fresh, std::ios::binary | std::ios::trunc);
		f.write(data.data(), (std::streamsize)data.size());
		if (!f)
		{
			SetFailed("Couldn't save the update next to the viewer (is the folder read-only?).");
			return;
		}
	}
	std::error_code ec;
	fs::remove(old, ec);
	if (!MoveFileExW(exe.wstring().c_str(), old.wstring().c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		fs::remove(fresh, ec);
		SetFailed("Couldn't replace the viewer (is the folder read-only?).");
		return;
	}
	if (!MoveFileExW(fresh.wstring().c_str(), exe.wstring().c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		MoveFileExW(old.wstring().c_str(), exe.wstring().c_str(), MOVEFILE_REPLACE_EXISTING); // put it back
		SetFailed("Couldn't put the update in place.");
		return;
	}
	{
		std::lock_guard<std::mutex> l(mutex);
		message = rel.value("name", tag);
	}
	state = State::Ready;
#endif
}

bool Updater::Relaunch(const fs::path& exe)
{
#ifdef _WIN32
	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	std::wstring cmd = L"\"" + exe.wstring() + L"\"";
	if (!CreateProcessW(exe.wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().wstring().c_str(), &si, &pi))
		return false;
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
#else
	(void)exe;
	return false;
#endif
}
