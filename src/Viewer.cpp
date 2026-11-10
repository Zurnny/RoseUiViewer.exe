#include "Viewer.h"
#include "CustomElements.h"
#include "GameScripting.h"
#include "ImageDecode.h"
#include "MarkupScan.h"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi/Lua.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <set>

extern const unsigned char g_font_regular[];
extern const size_t g_font_regular_size;
extern const unsigned char g_font_bold[];
extern const size_t g_font_bold_size;

using nlohmann::json;

static std::string NowText()
{
	std::time_t t = std::time(nullptr);
	char buf[32];
	std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
	return buf;
}

static std::string FileName(const std::string& ui_path)
{
	size_t s = ui_path.find_last_of('/');
	return s == std::string::npos ? ui_path : ui_path.substr(s + 1);
}

// ---------------------------------------------------------------- system interface

double ViewerSystemInterface::GetElapsedTime()
{
	if (base)
		return base->GetElapsedTime();
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

bool ViewerSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message)
{
	if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT)
		errors++;
	else if (type == Rml::Log::LT_WARNING)
		warnings++;
	if (type <= Rml::Log::LT_INFO)
	{
		log.push_back({type, message, NowText()});
		while (log.size() > 2000)
			log.pop_front();
	}
	if (base)
		base->LogMessage(type, message);
	else
		fprintf(stderr, "[%s] %s\n", type == Rml::Log::LT_ERROR ? "error" : type == Rml::Log::LT_WARNING ? "warning" : "info", message.c_str());
	return true;
}

void ViewerSystemInterface::JoinPath(Rml::String& out, const Rml::String& doc_path, const Rml::String& path)
{
	// A real path on this computer stays as it is (the stock version would strip the leading '/').
	if (!path.empty() && path[0] == '/' && GameFiles::Lower(path.substr(0, 6)) != "/data/")
	{
		std::error_code ec;
		if (fs::exists(fs::u8path(path), ec))
		{
			out = path;
			return;
		}
	}
	Rml::SystemInterface::JoinPath(out, doc_path, path);
}

// ---------------------------------------------------------------- setup

bool Viewer::Init(const fs::path& app_dir_in, Rml::RenderInterface* renderer, Rml::SystemInterface* platform_system, PlatformHooks hooks_in,
	Rml::Vector2i window_size, float content_scale_in)
{
	app_dir = app_dir_in;
	hooks = std::move(hooks_in);
	content_scale = content_scale_in;
	system.base = platform_system;
	mocks.SetFolder(app_dir / "mock");

	Rml::SetRenderInterface(renderer);
	Rml::SetSystemInterface(&system);
	Rml::SetFileInterface(&files);
	if (!Rml::Initialise())
		return false;
	Rml::Lua::Initialise();
	InstallGameScripting();
	SnapshotLuaGlobals();
	SetCommandHandler([this](Rml::Element* el, const std::string& name, const std::string& value) {
		// The model the element is in first, then any model with that variable.
		std::vector<std::string> models;
		for (Rml::Element* e = el; e; e = e->GetParentNode())
		{
			const Rml::String m = e->GetAttribute<Rml::String>("data-model", "");
			if (!m.empty())
			{
				models.push_back(m);
				break;
			}
		}
		for (auto& n : mocks.Names())
			models.push_back(n);
		for (auto& m : models)
		{
			json* j = mocks.Data(m);
			if (!j || !j->is_object() || !j->contains(name))
				continue;
			json& v = (*j)[name];
			try
			{
				size_t used = 0;
				long long n = std::stoll(value, &used);
				if (used == value.size())
					v = n;
				else
					v = std::stod(value);
			} catch (...)
			{
				v = value == "true" ? json(true) : value == "false" ? json(false) : json(value);
			}
			mocks.Changed(m);
			return true;
		}
		return false;
	});
	RegisterCustomElements();

	Rml::LoadFontFace({(const Rml::byte*)g_font_regular, g_font_regular_size}, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true);
	Rml::LoadFontFace({(const Rml::byte*)g_font_bold, g_font_bold_size}, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold, false);

	context = Rml::CreateContext("main", window_size);
	if (!context)
		return false;
	Rml::Debugger::Initialise(context);

	view_size = {(float)window_size.x, (float)window_size.y};
	LoadConfig();
	context->SetDensityIndependentPixelRatio(DpRatio());

	if (!HasGameFolder())
	{
		// Try the folder the viewer sits in, and a few usual install spots.
		std::vector<fs::path> guesses = {app_dir, app_dir.parent_path(), fs::current_path()};
#ifdef _WIN32
		for (const char* p : {"C:/Program Files (x86)/ROSE Online", "C:/Program Files/ROSE Online", "C:/ROSE Online", "C:/Games/ROSE Online",
				 "D:/ROSE Online", "D:/Games/ROSE Online"})
			guesses.push_back(fs::u8path(p));
#endif
		for (auto& g : guesses)
			if (GameFiles::LooksLikeGameFolder(g))
			{
				GameFiles::SetRoot(g);
				break;
			}
	}
	if (HasGameFolder())
	{
		std::error_code ec;
		fs::current_path(GameFiles::Root(), ec); // the theme's Lua opens files relative to the game folder
		LoadFonts();
	}

	ApplyBackground();
	ReloadAll();
	return true;
}

void Viewer::Shutdown()
{
	RememberPositions();
	SaveConfig();
	Rml::Shutdown();
}

void Viewer::LoadConfig()
{
	std::string text;
	if (!GameFiles::ReadAll(app_dir / "viewer_settings.json", text))
		return;
	try
	{
		json j = json::parse(text, nullptr, true, true);
		if (j.contains("game_folder"))
			GameFiles::SetRoot(fs::u8path(j["game_folder"].get<std::string>()));
		if (j.contains("open"))
			for (auto& o : j["open"])
				open_docs.push_back(o.get<std::string>());
		if (j.contains("positions"))
			for (auto& [k, v] : j["positions"].items())
				if (v.is_array() && v.size() == 2)
					positions[k] = {v[0].get<int>(), v[1].get<int>()};
		scale = j.value("scale", 1.0f);
		background_mode = j.value("background", std::string("dark"));
		auto_reload = j.value("auto_reload", true);
		if (j.contains("editor") && j["editor"].is_object())
			editor_settings = j["editor"];
	} catch (const std::exception& e)
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "viewer_settings.json couldn't be read: %s", e.what());
	}
}

void Viewer::SaveConfig()
{
	json j;
	j["game_folder"] = GameFiles::Root().u8string();
	j["open"] = open_docs;
	json pos = json::object();
	for (auto& [k, v] : positions)
		pos[k] = {v.x, v.y};
	j["positions"] = pos;
	j["scale"] = scale;
	j["background"] = background_mode;
	j["auto_reload"] = auto_reload;
	j["editor"] = editor_settings;
	GameFiles::WriteAll(app_dir / "viewer_settings.json", j.dump(2));
}

// Font data the viewer keeps for as long as it runs. RmlUi frees a font file's data early when a file holds
// several weights and one of them is already loaded, then reads the freed data for the next weight, which
// crashed the viewer when switching to another game folder with the same fonts. Fonts are therefore read
// here, kept, and each distinct file is loaded once.
static std::vector<std::unique_ptr<std::string>> g_font_data;
static std::set<std::string> g_font_keys;

static std::string FontKey(const std::string& bytes)
{
	uint64_t h = 1469598103934665603ull;
	for (unsigned char c : bytes)
	{
		h ^= c;
		h *= 1099511628211ull;
	}
	return std::to_string(bytes.size()) + ":" + std::to_string(h);
}

void Viewer::LoadFonts()
{
	// The game registers its fonts in C++; here every .ttf/.otf under Data/ is loaded instead.
	fs::path data = GameFiles::Resolve("Data");
	std::error_code ec;
	int count = 0, skipped = 0;
	for (fs::recursive_directory_iterator it(data, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
	{
		if (it.depth() > 5)
		{
			it.disable_recursion_pending();
			continue;
		}
		if (!it->is_regular_file(ec))
			continue;
		const std::string ext = GameFiles::Lower(it->path().extension().u8string());
		if (ext != ".ttf" && ext != ".otf")
			continue;
		auto bytes = std::make_unique<std::string>();
		if (!GameFiles::ReadAll(it->path(), *bytes) || bytes->size() < 12)
			continue;
		if (IsGitLfsPointer(*bytes))
		{
			Rml::Log::Message(Rml::Log::LT_WARNING,
				"Font %s is a Git LFS placeholder, not the real font. Install Git LFS and run 'git lfs pull' in that repo to get the real files.",
				it->path().u8string().c_str());
			continue;
		}
		if (!g_font_keys.insert(FontKey(*bytes)).second)
		{
			skipped++; // the same font is already loaded (e.g. from the previous game folder)
			continue;
		}
		const Rml::Span<const Rml::byte> span((const Rml::byte*)bytes->data(), bytes->size());
		g_font_data.push_back(std::move(bytes)); // kept for good: RmlUi reads it while drawing text
		if (Rml::LoadFontFace(span, "", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, count == 0))
			count++;
		else
			Rml::Log::Message(Rml::Log::LT_WARNING, "Couldn't load the font %s", it->path().u8string().c_str());
	}
	Rml::Log::Message(Rml::Log::LT_INFO, "Loaded %d font files from the game folder%s", count,
		skipped ? (" (" + std::to_string(skipped) + " already loaded)").c_str() : "");
}

bool Viewer::HasGameFolder() const { return GameFiles::LooksLikeGameFolder(GameFiles::Root()); }

bool Viewer::SetGameFolder(const fs::path& folder)
{
	if (!GameFiles::LooksLikeGameFolder(folder))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "%s doesn't look like a ROSE Online folder (no Data\\ui\\themes in it)", folder.u8string().c_str());
		return false;
	}
	const fs::path previous = GameFiles::Root();
	const std::vector<std::string> previous_docs = open_docs;
	try
	{
		const bool changed = GameFiles::Key(folder) != GameFiles::Key(previous);
		GameFiles::SetRoot(folder);
		std::error_code ec;
		fs::current_path(folder, ec);
		if (changed)
			open_docs.clear();
		LoadFonts(); // each distinct font file is only loaded once, so this is cheap when nothing is new
		doc_list_dirty = true;
		SaveConfig();
		ReloadAll();
		Rml::Log::Message(Rml::Log::LT_INFO, "Game folder: %s", folder.u8string().c_str());
		return true;
	} catch (const std::exception& e)
	{
		// Go back to the folder that worked.
		Rml::Log::Message(Rml::Log::LT_ERROR, "Couldn't switch to %s: %s", folder.u8string().c_str(), e.what());
		GameFiles::SetRoot(previous);
		std::error_code ec;
		fs::current_path(previous, ec);
		open_docs = previous_docs;
		doc_list_dirty = true;
		try
		{
			SaveConfig();
			ReloadAll();
		} catch (...)
		{
		}
		return false;
	}
}

void Viewer::PickGameFolder()
{
	if (!hooks.pick_folder)
		return;
	fs::path f = hooks.pick_folder();
	if (f.empty())
		return;
	fs::path game = GameFiles::LooksLikeGameFolder(f) ? f : GameFiles::FindGameFolderAbove(f);
	if (game.empty())
		Rml::Log::Message(Rml::Log::LT_WARNING, "That folder has no Data\\ui\\themes in it. Pick the ROSE Online folder itself.");
	else
		pending_folder = game; // switched at the start of the next frame, outside the editor's drawing
}

// ---------------------------------------------------------------- documents

const std::vector<std::string>& Viewer::ListGameDocuments()
{
	if (!doc_list_dirty)
		return doc_list;
	doc_list_dirty = false;
	doc_list.clear();
	if (!HasGameFolder())
		return doc_list;
	fs::path themes = GameFiles::Resolve("Data/ui/themes");
	std::error_code ec;
	for (fs::recursive_directory_iterator it(themes, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file(ec))
			continue;
		const std::string ext = GameFiles::Lower(it->path().extension().u8string());
		if (ext != ".html" && ext != ".rml")
			continue;
		std::string ui = GameFiles::ToUiPath(it->path());
		if (GameFiles::Lower(ui).find("/templates/") != std::string::npos)
			continue;
		doc_list.push_back(ui);
	}
	std::sort(doc_list.begin(), doc_list.end(), [](const std::string& a, const std::string& b) { return GameFiles::Lower(a) < GameFiles::Lower(b); });
	return doc_list;
}

void Viewer::OpenDocument(const std::string& ui_path)
{
	auto it = std::find(open_docs.begin(), open_docs.end(), ui_path);
	if (it != open_docs.end())
	{
		// Already open: bring it to the top.
		open_docs.erase(it);
		open_docs.push_back(ui_path);
		if (auto* d = LoadedDocument(ui_path))
			BringToFront(d);
		return;
	}
	open_docs.push_back(ui_path);
	reload_requested = true; // loaded at the start of the next frame
}

void Viewer::CloseDocument(const std::string& ui_path)
{
	RememberPositions();
	open_docs.erase(std::remove(open_docs.begin(), open_docs.end(), ui_path), open_docs.end());
	reload_requested = true;
}

Rml::ElementDocument* Viewer::LoadedDocument(const std::string& ui_path) const
{
	auto it = loaded.find(ui_path);
	return it == loaded.end() ? nullptr : it->second;
}

std::string Viewer::PathOf(Rml::ElementDocument* doc) const
{
	for (auto& [p, d] : loaded)
		if (d == doc)
			return p;
	return {};
}

const WindowFiles* Viewer::FilesOf(const std::string& ui_path) const
{
	auto it = window_files.find(ui_path);
	return it == window_files.end() ? nullptr : &it->second;
}

void Viewer::BringToFront(Rml::ElementDocument* doc)
{
	if (!doc)
		return;
	doc->PullToFront();
	if (background)
		background->PushToBack();
}

void Viewer::RememberPositions()
{
	for (auto& [path, doc] : loaded)
		if (doc)
		{
			// The margin box's corner is what left/top place (the border box would add the body's margin each reload).
			Rml::Vector2f p = doc->GetAbsoluteOffset(Rml::BoxArea::Margin);
			positions[path] = {(int)std::round(p.x), (int)std::round(p.y)};
		}
}

// Places a window by its left/top. Any right/bottom anchoring from its stylesheet is switched off in the
// preview, or the window would stretch between left and right.
static void PlaceDocument(Rml::ElementDocument* d, float x, float y)
{
	d->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
	d->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
	d->SetProperty("right", "auto");
	d->SetProperty("bottom", "auto");
}

void Viewer::SetDocumentPosition(const std::string& ui_path, int x, int y)
{
	positions[ui_path] = {x, y};
	if (auto* d = LoadedDocument(ui_path))
		PlaceDocument(d, (float)x, (float)y);
}

// The area a window actually covers on screen: its own box plus anything inside that sticks out of it.
static void VisibleBounds(Rml::Element* e, Rml::Vector2f& lo, Rml::Vector2f& hi, bool& any)
{
	if (!e->IsVisible() || e->GetTagName() == "#text")
		return;
	const Rml::Vector2f p = e->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f s = e->GetBox().GetSize(Rml::BoxArea::Border);
	if (s.x > 0 && s.y > 0)
	{
		if (!any)
		{
			lo = p;
			hi = p + s;
			any = true;
		}
		lo.x = std::min(lo.x, p.x);
		lo.y = std::min(lo.y, p.y);
		hi.x = std::max(hi.x, p.x + s.x);
		hi.y = std::max(hi.y, p.y + s.y);
	}
	for (int i = 0; i < e->GetNumChildren(); i++)
		VisibleBounds(e->GetChild(i), lo, hi, any);
}

void Viewer::CenterDocument(const std::string& ui_path, float offset)
{
	Rml::ElementDocument* d = LoadedDocument(ui_path);
	if (!d)
		return;
	// Start from a known place so right/bottom anchoring doesn't skew the measurement.
	Rml::Vector2f cur = d->GetAbsoluteOffset(Rml::BoxArea::Margin);
	PlaceDocument(d, cur.x, cur.y);
	context->Update();
	cur = d->GetAbsoluteOffset(Rml::BoxArea::Margin);
	Rml::Vector2f lo, hi;
	bool any = false;
	VisibleBounds(d, lo, hi, any);
	if (!any)
	{
		lo = cur;
		hi = cur;
	}
	const Rml::Vector2f size = hi - lo;
	// Centre in the free area; if it's bigger than the area, line its top-left up with the area's.
	const float tx = view_pos.x + std::max(0.f, (view_size.x - size.x) * 0.5f) + offset;
	const float ty = view_pos.y + std::max(0.f, (view_size.y - size.y) * 0.5f) + offset;
	const float x = std::round(cur.x + (tx - lo.x));
	const float y = std::round(cur.y + (ty - lo.y));
	PlaceDocument(d, x, y);
	positions[ui_path] = {(int)x, (int)y};
}

void Viewer::CenterAll()
{
	int n = 0;
	for (const auto& ui : open_docs)
		CenterDocument(ui, (n++) * 24.f);
	SaveConfig();
}

void Viewer::ReloadAll()
{
	if (!context)
		return;
	reload_requested = false;
	RememberPositions();

	for (auto& [path, doc] : loaded)
		if (doc)
			doc->Close();
	loaded.clear();
	context->Update(); // let the closed documents go before their data models are removed
	ResetLuaGlobals();

	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
	Rml::ReleaseTextures();
	mocks.RemoveAll(context);
	system.errors = system.warnings = 0;
	GameFiles::ClearAnnotated();
	window_files.clear();

	// Work out every data model the open windows use (including their templates and scripts),
	// and which files each window is made of.
	std::map<std::string, ModelScan> models;
	std::vector<std::string> keep;
	for (const auto& ui : open_docs)
	{
		if (!HasGameFolder())
		{
			keep.push_back(ui); // reopened once the folder is back (the web version opens it after start-up)
			continue;
		}
		std::string text;
		fs::path real = GameFiles::Resolve(ui);
		if (!GameFiles::ReadCurrent(real, text))
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "%s isn't there any more, so it was closed", ui.c_str());
			continue;
		}
		keep.push_back(ui);
		WindowFiles& wf = window_files[ui];
		wf.document = ui;
		GameFiles::AddAnnotated(real);

		std::vector<std::pair<std::string, std::string>> queue = {{ui, text}};
		std::set<std::string> seen = {GameFiles::Lower(ui)};
		auto add_unique = [](std::vector<std::string>& v, const std::string& s) {
			if (std::find(v.begin(), v.end(), s) == v.end())
				v.push_back(s);
		};
		while (!queue.empty())
		{
			auto [path, body] = queue.back();
			queue.pop_back();
			DocScan scan = ScanDocument(body);
			for (auto& [name, m] : scan.models)
			{
				auto& dst = models[name];
				dst.vars.insert(dst.vars.end(), m.vars.begin(), m.vars.end());
				dst.events.insert(m.events.begin(), m.events.end());
				dst.transforms.insert(m.transforms.begin(), m.transforms.end());
			}
			for (auto& href : scan.stylesheets)
			{
				Rml::String joined;
				system.JoinPath(joined, path, href);
				add_unique(wf.stylesheets, joined);
			}
			auto follow = [&](const std::vector<std::string>& hrefs, std::vector<std::string>& list, bool annotate) {
				for (auto& href : hrefs)
				{
					Rml::String joined;
					system.JoinPath(joined, path, href);
					add_unique(list, joined);
					if (!seen.insert(GameFiles::Lower(joined)).second)
						continue;
					std::string t;
					fs::path r = GameFiles::Resolve(joined);
					if (GameFiles::ReadCurrent(r, t))
					{
						if (annotate)
							GameFiles::AddAnnotated(r);
						queue.push_back({joined, t});
					}
				}
			};
			follow(scan.templates, wf.templates, true);
			follow(scan.scripts, wf.scripts, false);
		}
	}
	open_docs = keep;
	mocks.Build(context, models);

	// Load the windows, in the order they were opened (last on top).
	int cascade = 0;
	for (const auto& ui : open_docs)
	{
		if (!HasGameFolder())
			break;
		Rml::ElementDocument* doc = context->LoadDocument(ui);
		if (!doc)
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "Couldn't load %s", ui.c_str());
			continue;
		}
		loaded[ui] = doc;
		doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
		RunDeferredScripts(doc);
		auto pos = positions.find(ui);
		if (pos != positions.end())
			PlaceDocument(doc, (float)pos->second.x, (float)pos->second.y);
		else
		{
			// First time: centre it in the free area (a little offset per window so they don't stack exactly).
			context->Update();
			CenterDocument(ui, cascade * 24.f);
			cascade++;
		}
	}
	if (background)
		background->PushToBack();

	last_reload_time = NowText();
	Rml::Log::Message(Rml::Log::LT_INFO, "Reloaded %d window(s) at %s", (int)loaded.size(), last_reload_time.c_str());
	context->Update();
	UpdateTitle();
	SaveConfig();
	last_fingerprint = Fingerprint();
	change_pending = false;
	if (on_reloaded)
		on_reloaded();
}

float Viewer::DpRatio() const { return scale * content_scale; }

void Viewer::SetScale(float s)
{
	scale = std::clamp(s, 0.5f, 3.0f);
	context->SetDensityIndependentPixelRatio(DpRatio());
	SaveConfig();
}

void Viewer::SetBackground(const std::string& mode)
{
	background_mode = mode;
	ApplyBackground();
	SaveConfig();
}

void Viewer::UpdateTitle()
{
	if (!hooks.set_title)
		return;
	std::string t = "ROSE UI Viewer";
	if (!open_docs.empty())
		t += " - " + FileName(open_docs.back());
	hooks.set_title(t);
}

void Viewer::ApplyBackground()
{
	if (!background)
	{
		background = context->LoadDocumentFromMemory(
			"<rml><head><title>viewer-background</title><style>body { position: absolute; left: 0; top: 0; width: 100%; height: 100%; z-index: -1000; }</style></head><body id=\"viewer-background\"></body></rml>",
			"[viewer background]");
		if (!background)
			return;
		background->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	}
	std::string color = "#2b2d31";
	if (background_mode == "grey")
		color = "#6e6e6e";
	else if (background_mode == "light")
		color = "#d8d8d8";
	else if (background_mode == "black")
		color = "#000000";
	else if (background_mode == "green")
		color = "#00b140";
	background->SetProperty("background-color", color);
	background->RemoveProperty("decorator");
	if (background_mode == "image")
	{
		fs::path img;
		for (const char* name : {"background.png", "background.jpg", "background.jpeg", "background.tga"})
		{
			std::error_code ec;
			if (fs::exists(app_dir / name, ec))
			{
				img = app_dir / name;
				break;
			}
		}
		if (img.empty())
			Rml::Log::Message(Rml::Log::LT_WARNING, "For the Screenshot background, put a picture named background.png (or .jpg) next to the viewer: %s",
				app_dir.u8string().c_str());
		else
		{
			std::string p = img.u8string();
			std::replace(p.begin(), p.end(), '\\', '/');
			background->SetProperty("decorator", "image(\"" + p + "\" cover)");
		}
	}
	background->PushToBack();
}

// ---------------------------------------------------------------- watching for edits

uint64_t Viewer::Fingerprint() const
{
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](uint64_t v) {
		h ^= v;
		h *= 1099511628211ull;
	};
	auto walk = [&](const fs::path& dir) {
		std::error_code ec;
		for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_regular_file(ec))
				continue;
			auto t = fs::last_write_time(it->path(), ec);
			mix((uint64_t)t.time_since_epoch().count());
			mix((uint64_t)it->file_size(ec));
			for (char c : it->path().filename().u8string())
				mix((uint64_t)(unsigned char)c);
		}
	};
	if (HasGameFolder())
		walk(GameFiles::Resolve("Data/ui"));
	walk(mocks.Folder());
	return h;
}

void Viewer::NoteOwnWrite()
{
	last_fingerprint = Fingerprint();
	change_pending = false;
}

void Viewer::Update()
{
	using namespace std::chrono;
	const auto now = steady_clock::now();
	if (!pending_folder.empty())
	{
		fs::path f = std::move(pending_folder);
		pending_folder.clear();
		SetGameFolder(f);
	}
	if (reload_requested)
		ReloadAll();
	if (auto_reload && now - last_check > milliseconds(600))
	{
		last_check = now;
		const uint64_t fp = Fingerprint();
		if (fp != last_fingerprint)
		{
			last_fingerprint = fp;
			change_pending = true;
			change_seen = now;
			doc_list_dirty = true;
		}
	}
	// Wait a moment after the last change: editors often save in more than one step.
	if (change_pending && now - change_seen > milliseconds(250))
		ReloadAll();
	context->Update();
}

void Viewer::Render()
{
	context->Update(); // apply anything the editor changed this frame
	context->Render();
}

// ---------------------------------------------------------------- input

bool Viewer::OnKeyDown(Rml::Input::KeyIdentifier key, int /*modifiers*/)
{
	using namespace Rml::Input;
	switch (key)
	{
	case KI_F5: ReloadAll(); return true;
	case KI_F8: Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible()); return true;
	default: break;
	}
	return false;
}

void Viewer::OnDrop(const std::vector<fs::path>& paths)
{
	bool any = false;
	for (const auto& p : paths)
	{
		std::error_code ec;
		if (fs::is_directory(p, ec))
		{
			fs::path game = GameFiles::LooksLikeGameFolder(p) ? p : GameFiles::FindGameFolderAbove(p);
			if (!game.empty())
				SetGameFolder(game);
			continue;
		}
		const std::string ext = GameFiles::Lower(p.extension().u8string());
		if (ext != ".html" && ext != ".rml")
			continue;
		fs::path game = GameFiles::FindGameFolderAbove(p.parent_path());
		if (game.empty())
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "%s isn't inside a ROSE Online folder", p.u8string().c_str());
			continue;
		}
		if (game != GameFiles::Root())
			SetGameFolder(game);
		std::string ui = GameFiles::ToUiPath(p);
		if (std::find(open_docs.begin(), open_docs.end(), ui) == open_docs.end())
			open_docs.push_back(ui);
		any = true;
	}
	if (any)
		ReloadAll();
}
