#include "GameFiles.h"
#include "SourceEdit.h"
#include <map>
#include <mutex>
#include <RmlUi/Core/Log.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstring>

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
			if (Lower(it->path().filename().u8string()) == want)
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

static std::function<void(const fs::path&)> g_write_hook;

void SetWriteHook(std::function<void(const fs::path&)> hook) { g_write_hook = std::move(hook); }

bool WriteAll(const fs::path& file, const std::string& data)
{
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	{
		std::ofstream f(file, std::ios::binary | std::ios::trunc);
		if (!f)
			return false;
		f << data;
		if (!f)
			return false;
	}
	if (g_write_hook)
		g_write_hook(file);
	return true;
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

static std::map<std::string, std::string> g_overlays;
static std::vector<fs::path> g_annotated;
static std::map<std::string, int> g_annotated_index;

std::string Key(const fs::path& real)
{
	std::error_code ec;
	fs::path c = fs::weakly_canonical(real, ec);
	std::string s = (ec ? real : c).generic_u8string();
	return Lower(s);
}

void SetOverlay(const fs::path& real, const std::string& text) { g_overlays[Key(real)] = text; }
void ClearOverlay(const fs::path& real) { g_overlays.erase(Key(real)); }
bool HasOverlay(const fs::path& real) { return g_overlays.count(Key(real)) > 0; }

bool ReadCurrent(const fs::path& real, std::string& out)
{
	auto it = g_overlays.find(Key(real));
	if (it != g_overlays.end())
	{
		out = it->second;
		return true;
	}
	return ReadAll(real, out);
}

void ClearAnnotated()
{
	g_annotated.clear();
	g_annotated_index.clear();
}

int AddAnnotated(const fs::path& real)
{
	const std::string k = Key(real);
	auto it = g_annotated_index.find(k);
	if (it != g_annotated_index.end())
		return it->second;
	int idx = (int)g_annotated.size();
	g_annotated.push_back(real);
	g_annotated_index[k] = idx;
	return idx;
}

fs::path AnnotatedFile(int index)
{
	if (index < 0 || index >= (int)g_annotated.size())
		return {};
	return g_annotated[index];
}

static int AnnotatedIndex(const fs::path& real)
{
	auto it = g_annotated_index.find(Key(real));
	return it == g_annotated_index.end() ? -1 : it->second;
}

// Content as the UI should see it: overlay or disk, with source tags for annotated files.
static bool ServeFile(const fs::path& real, std::string& out)
{
	if (!ReadCurrent(real, out))
		return false;
	int idx = AnnotatedIndex(real);
	if (idx >= 0)
		out = SourceEdit::Annotate(out, idx);
	return true;
}

bool Serve(const fs::path& real, std::string& out) { return ServeFile(real, out); }

} // namespace GameFiles

// ---------------------------------------------------------------- file interface

namespace {
struct MemFile {
	std::string data;
	size_t pos = 0;
};
} // namespace

Rml::FileHandle GameFileInterface::Open(const Rml::String& path)
{
	auto f = new MemFile();
	if (!GameFiles::Serve(GameFiles::Resolve(path), f->data))
	{
		delete f;
		return 0;
	}
	return (Rml::FileHandle)f;
}

void GameFileInterface::Close(Rml::FileHandle file) { delete (MemFile*)file; }

size_t GameFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file)
{
	auto f = (MemFile*)file;
	size_t n = std::min(size, f->data.size() - f->pos);
	memcpy(buffer, f->data.data() + f->pos, n);
	f->pos += n;
	return n;
}

bool GameFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
{
	auto f = (MemFile*)file;
	long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (long)f->pos : (long)f->data.size();
	long p = base + offset;
	if (p < 0 || p > (long)f->data.size())
		return false;
	f->pos = (size_t)p;
	return true;
}

size_t GameFileInterface::Tell(Rml::FileHandle file) { return ((MemFile*)file)->pos; }

size_t GameFileInterface::Length(Rml::FileHandle file) { return ((MemFile*)file)->data.size(); }

bool GameFileInterface::LoadFile(const Rml::String& path, Rml::String& out_data)
{
	if (!GameFiles::Serve(GameFiles::Resolve(path), out_data))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "File not found: %s", path.c_str());
		return false;
	}
	return true;
}
