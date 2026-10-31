#include "GameFiles.h"
#include <RmlUi/Core/Log.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace GameFiles {

static fs::path g_root;

void SetRoot(const fs::path& root) { g_root = root; }
const fs::path& Root() { return g_root; }

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

static std::vector<std::string> SplitPath(const std::string& p)
{
	std::vector<std::string> parts;
	std::string cur;
	for (char c : p)
	{
		if (c == '/' || c == '\\')
		{
			if (!cur.empty())
				parts.push_back(cur);
			cur.clear();
		}
		else
			cur += c;
	}
	if (!cur.empty())
		parts.push_back(cur);
	// Collapse "." and ".."
	std::vector<std::string> out;
	for (auto& s : parts)
	{
		if (s == ".")
			continue;
		if (s == "..")
		{
			if (!out.empty() && out.back() != "..")
				out.pop_back();
			else
				out.push_back(s);
			continue;
		}
		out.push_back(s);
	}
	return out;
}

// Finds 'parts' below 'base', matching each part without regard to case.
static fs::path ResolveNoCase(const fs::path& base, const std::vector<std::string>& parts)
{
	fs::path cur = base;
	for (const auto& part : parts)
	{
		fs::path direct = cur / part;
		std::error_code ec;
		if (fs::exists(direct, ec))
		{
			cur = direct;
			continue;
		}
		if (part == "..")
		{
			cur = cur.parent_path();
			continue;
		}
		bool found = false;
		const std::string want = Lower(part);
		for (fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec))
		{
			if (Lower(it->path().filename().string()) == want)
			{
				cur = it->path();
				found = true;
				break;
			}
		}
		if (!found)
			return base / fs::path(part); // not found; return something sensible for the error message
	}
	return cur;
}

fs::path Resolve(const std::string& ui_path_in)
{
	std::string ui_path = ui_path_in;
	std::replace(ui_path.begin(), ui_path.end(), '\\', '/');

	auto parts = SplitPath(ui_path);
	const bool leading_slash = !ui_path.empty() && ui_path[0] == '/';

	// "/data/..." means the game's Data folder.
	if (!parts.empty() && Lower(parts[0]) == "data" && !g_root.empty())
		return ResolveNoCase(g_root, parts);

	// A real path on this computer (C:/..., /home/..., viewer files).
	std::error_code ec;
	fs::path as_is = fs::u8path(ui_path);
	if ((as_is.is_absolute() || leading_slash) && fs::exists(as_is, ec))
		return as_is;

	if (!g_root.empty())
		return ResolveNoCase(g_root, parts);
	return as_is;
}

std::string ToUiPath(const fs::path& real)
{
	std::error_code ec;
	fs::path rel = fs::relative(real, g_root, ec);
	std::string s = (ec || rel.empty()) ? real.u8string() : rel.u8string();
	std::replace(s.begin(), s.end(), '\\', '/');
	return s;
}

bool ReadAll(const fs::path& file, std::string& out)
{
	std::ifstream f(file, std::ios::binary);
	if (!f)
		return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

bool WriteAll(const fs::path& file, const std::string& data)
{
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	std::ofstream f(file, std::ios::binary | std::ios::trunc);
	if (!f)
		return false;
	f << data;
	return (bool)f;
}

bool LooksLikeGameFolder(const fs::path& folder)
{
	if (folder.empty())
		return false;
	fs::path themes = ResolveNoCase(folder, {"Data", "ui", "themes"});
	std::error_code ec;
	return fs::is_directory(themes, ec);
}

fs::path FindGameFolderAbove(const fs::path& file)
{
	fs::path p = file;
	for (int i = 0; i < 12 && !p.empty(); i++)
	{
		if (LooksLikeGameFolder(p))
			return p;
		fs::path parent = p.parent_path();
		if (parent == p)
			break;
		p = parent;
	}
	return {};
}

} // namespace GameFiles

// ---------------------------------------------------------------- file interface

Rml::FileHandle GameFileInterface::Open(const Rml::String& path)
{
	fs::path real = GameFiles::Resolve(path);
#ifdef _WIN32
	FILE* f = _wfopen(real.wstring().c_str(), L"rb");
#else
	FILE* f = fopen(real.string().c_str(), "rb");
#endif
	return (Rml::FileHandle)f;
}

void GameFileInterface::Close(Rml::FileHandle file) { fclose((FILE*)file); }

size_t GameFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file) { return fread(buffer, 1, size, (FILE*)file); }

bool GameFileInterface::Seek(Rml::FileHandle file, long offset, int origin) { return fseek((FILE*)file, offset, origin) == 0; }

size_t GameFileInterface::Tell(Rml::FileHandle file) { return (size_t)ftell((FILE*)file); }

size_t GameFileInterface::Length(Rml::FileHandle file)
{
	FILE* f = (FILE*)file;
	long cur = ftell(f);
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, cur, SEEK_SET);
	return (size_t)len;
}

bool GameFileInterface::LoadFile(const Rml::String& path, Rml::String& out_data)
{
	fs::path real = GameFiles::Resolve(path);
	if (!GameFiles::ReadAll(real, out_data))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "File not found: %s", path.c_str());
		return false;
	}
	return true;
}
