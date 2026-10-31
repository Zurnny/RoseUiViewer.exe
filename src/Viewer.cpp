#include "Viewer.h"
#include "CustomElements.h"
#include "GameScripting.h"
#include "MarkupScan.h"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi/Lua.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <set>

extern const unsigned char g_font_regular[];
extern const size_t g_font_regular_size;
extern const unsigned char g_font_bold[];
extern const size_t g_font_bold_size;

using nlohmann::json;

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
	if (type <= Rml::Log::LT_WARNING)
	{
		recent.push_back(message);
		if (recent.size() > 200)
			recent.erase(recent.begin());
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

// ---------------------------------------------------------------- helpers

static std::string Escape(const std::string& s)
{
	std::string o;
	for (char c : s)
	{
		switch (c)
		{
		case '&': o += "&amp;"; break;
		case '<': o += "&lt;"; break;
		case '>': o += "&gt;"; break;
		case '"': o += "&quot;"; break;
		default: o += c;
		}
	}
	return o;
}

static std::string FileName(const std::string& ui_path)
{
	size_t s = ui_path.find_last_of('/');
	return s == std::string::npos ? ui_path : ui_path.substr(s + 1);
}

static std::string NowText()
{
	std::time_t t = std::time(nullptr);
	char buf[32];
	std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
	return buf;
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
	RegisterCustomElements();

	Rml::LoadFontFace({(const Rml::byte*)g_font_regular, g_font_regular_size}, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true);
	Rml::LoadFontFace({(const Rml::byte*)g_font_bold, g_font_bold_size}, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold, false);

	context = Rml::CreateContext("main", window_size);
	if (!context)
		return false;
	Rml::Debugger::Initialise(context);

	LoadConfig();
	context->SetDensityIndependentPixelRatio(scale * content_scale);

	if (!GameFiles::LooksLikeGameFolder(GameFiles::Root()))
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
	if (GameFiles::LooksLikeGameFolder(GameFiles::Root()))
	{
		std::error_code ec;
		fs::current_path(GameFiles::Root(), ec); // the theme's Lua opens files relative to the game folder
		LoadFonts();
	}

	BuildToolbar();
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
		toolbar_visible = j.value("toolbar", true);
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
	j["toolbar"] = toolbar_visible;
	GameFiles::WriteAll(app_dir / "viewer_settings.json", j.dump(2));
}

void Viewer::LoadFonts()
{
	// The game registers its fonts in C++; here every .ttf/.otf under Data/ is loaded instead.
	fs::path data = GameFiles::Resolve("Data");
	std::error_code ec;
	int count = 0;
	for (fs::recursive_directory_iterator it(data, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
	{
		if (it.depth() > 5)
		{
			it.disable_recursion_pending();
			continue;
		}
		if (!it->is_regular_file(ec))
			continue;
		const std::string ext = GameFiles::Lower(it->path().extension().string());
		if (ext == ".ttf" || ext == ".otf")
		{
			if (Rml::LoadFontFace(it->path().u8string(), count == 0))
				count++;
		}
	}
	Rml::Log::Message(Rml::Log::LT_INFO, "Loaded %d font files from the game folder", count);
}

bool Viewer::SetGameFolder(const fs::path& folder)
{
	if (!GameFiles::LooksLikeGameFolder(folder))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "%s doesn't look like a ROSE Online folder (no Data\\ui\\themes in it)", folder.u8string().c_str());
		return false;
	}
	const bool changed = folder != GameFiles::Root();
	GameFiles::SetRoot(folder);
	std::error_code ec;
	fs::current_path(folder, ec);
	if (changed)
	{
		LoadFonts();
		open_docs.clear();
	}
	SaveConfig();
	BuildToolbar();
	ReloadAll();
	return true;
}

// ---------------------------------------------------------------- documents

std::vector<std::string> Viewer::ListGameDocuments() const
{
	std::vector<std::string> out;
	if (GameFiles::Root().empty())
		return out;
	fs::path themes = GameFiles::Resolve("Data/ui/themes");
	std::error_code ec;
	for (fs::recursive_directory_iterator it(themes, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file(ec))
			continue;
		const std::string ext = GameFiles::Lower(it->path().extension().string());
		if (ext != ".html" && ext != ".rml")
			continue;
		std::string ui = GameFiles::ToUiPath(it->path());
		const std::string low = GameFiles::Lower(ui);
		if (low.find("/templates/") != std::string::npos)
			continue;
		out.push_back(ui);
	}
	std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return GameFiles::Lower(a) < GameFiles::Lower(b); });
	return out;
}

void Viewer::OpenDocument(const std::string& ui_path)
{
	if (std::find(open_docs.begin(), open_docs.end(), ui_path) == open_docs.end())
		open_docs.push_back(ui_path);
	ReloadAll();
}

void Viewer::CloseDocument(const std::string& ui_path)
{
	RememberPositions();
	open_docs.erase(std::remove(open_docs.begin(), open_docs.end(), ui_path), open_docs.end());
	ReloadAll();
}

void Viewer::RememberPositions()
{
	for (auto& [path, doc] : loaded)
		if (doc)
		{
			Rml::Vector2f p = doc->GetAbsoluteOffset(Rml::BoxArea::Border);
			positions[path] = {(int)std::round(p.x), (int)std::round(p.y)};
		}
}

void Viewer::SetDocumentPosition(const std::string& ui_path, int x, int y)
{
	positions[ui_path] = {x, y};
	auto it = loaded.find(ui_path);
	if (it != loaded.end() && it->second)
	{
		it->second->SetProperty(Rml::PropertyId::Left, Rml::Property((float)x, Rml::Unit::PX));
		it->second->SetProperty(Rml::PropertyId::Top, Rml::Property((float)y, Rml::Unit::PX));
	}
}

void Viewer::ReloadAll()
{
	if (!context)
		return;
	RememberPositions();

	for (auto& [path, doc] : loaded)
		if (doc)
			doc->Close();
	loaded.clear();
	context->Update(); // let the closed documents go before their data models are removed

	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
	Rml::ReleaseTextures();
	mocks.RemoveAll(context);
	system.errors = system.warnings = 0;
	system.recent.clear();

	// Work out every data model the open windows use (including their templates).
	std::map<std::string, ModelScan> models;
	std::vector<std::string> keep;
	for (const auto& ui : open_docs)
	{
		std::string text;
		fs::path real = GameFiles::Resolve(ui);
		if (!GameFiles::ReadAll(real, text))
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "%s isn't there any more, so it was closed", ui.c_str());
			continue;
		}
		keep.push_back(ui);
		std::vector<std::pair<std::string, std::string>> queue = {{ui, text}};
		std::set<std::string> seen = {GameFiles::Lower(ui)};
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
			std::vector<std::string> linked = scan.templates;
			linked.insert(linked.end(), scan.scripts.begin(), scan.scripts.end());
			for (auto& href : linked)
			{
				Rml::String joined;
				system.JoinPath(joined, path, href);
				if (!seen.insert(GameFiles::Lower(joined)).second)
					continue;
				std::string t;
				if (GameFiles::ReadAll(GameFiles::Resolve(joined), t))
					queue.push_back({joined, t});
			}
		}
	}
	open_docs = keep;
	mocks.Build(context, models);

	// Load the windows, in the order they were opened (last on top).
	int cascade = 0;
	for (const auto& ui : open_docs)
	{
		Rml::ElementDocument* doc = context->LoadDocument(ui);
		if (!doc)
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "Couldn't load %s", ui.c_str());
			continue;
		}
		loaded[ui] = doc;
		doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
		auto pos = positions.find(ui);
		if (pos != positions.end())
		{
			doc->SetProperty(Rml::PropertyId::Left, Rml::Property((float)pos->second.x, Rml::Unit::PX));
			doc->SetProperty(Rml::PropertyId::Top, Rml::Property((float)pos->second.y, Rml::Unit::PX));
		}
		else
		{
			// First time: centre it (a little offset per window so they don't stack exactly).
			context->Update();
			Rml::Vector2f size = doc->GetBox().GetSize(Rml::BoxArea::Border);
			Rml::Vector2i dims = context->GetDimensions();
			float x = std::max(0.f, (dims.x - size.x) * 0.5f) + cascade * 24.f;
			float y = std::max(toolbar_visible ? 64.f : 0.f, (dims.y - size.y) * 0.5f) + cascade * 24.f;
			doc->SetProperty(Rml::PropertyId::Left, Rml::Property(std::round(x), Rml::Unit::PX));
			doc->SetProperty(Rml::PropertyId::Top, Rml::Property(std::round(y), Rml::Unit::PX));
			positions[ui] = {(int)std::round(x), (int)std::round(y)};
			cascade++;
		}
	}
	if (background)
		background->PushToBack();
	if (toolbar)
		toolbar->PullToFront();

	last_reload_time = NowText();
	Rml::Log::Message(Rml::Log::LT_INFO, "Reloaded %d window(s) at %s", (int)loaded.size(), last_reload_time.c_str());
	context->Update();
	UpdateToolbarStatus();
	UpdateTitle();
	SaveConfig();
	last_fingerprint = Fingerprint();
	change_pending = false;
}

void Viewer::SetScale(float s)
{
	scale = std::clamp(s, 0.5f, 3.0f);
	context->SetDensityIndependentPixelRatio(scale * content_scale);
	BuildToolbar();
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
			for (char c : it->path().filename().string())
				mix((uint64_t)(unsigned char)c);
		}
	};
	if (!GameFiles::Root().empty())
		walk(GameFiles::Resolve("Data/ui"));
	walk(mocks.Folder());
	return h;
}

void Viewer::Update()
{
	using namespace std::chrono;
	const auto now = steady_clock::now();
	if (!pending.empty())
	{
		auto actions = std::move(pending);
		pending.clear();
		for (auto& a : actions)
			a();
	}
	if (reload_requested)
	{
		reload_requested = false;
		ReloadAll();
	}
	if (auto_reload && now - last_check > milliseconds(600))
	{
		last_check = now;
		const uint64_t fp = Fingerprint();
		if (fp != last_fingerprint)
		{
			last_fingerprint = fp;
			change_pending = true;
			change_seen = now;
		}
	}
	// Wait a moment after the last change: editors often save in more than one step.
	if (change_pending && now - change_seen > milliseconds(250))
		ReloadAll();

	static int status_errors = -1, status_warnings = -1;
	if (status_errors != system.errors || status_warnings != system.warnings)
	{
		status_errors = system.errors;
		status_warnings = system.warnings;
		UpdateToolbarStatus();
	}
	context->Update();
}

void Viewer::Render() { context->Render(); }

// ---------------------------------------------------------------- toolbar

static const char* kToolbarStyle = R"(
body { font-family: Lato; font-size: 14px; color: #ece3d0; width: 100%; height: 64px; left: 0; top: 0; z-index: 1000; }
#bar { display: flex; flex-direction: row; align-items: center; height: 38px; padding: 0 8px; background-color: #1f1b17f2; border-bottom: 1px #5c4a30; }
#status { display: flex; flex-direction: row; align-items: center; height: 24px; padding: 0 44px 0 10px; background-color: #15120fe6; font-size: 13px; color: #b9ab90; }
#status .grow { flex: 1; }
#status .err { color: #ff8a70; margin-left: 10px; }
#status .warn { color: #f0c060; margin-left: 10px; }
#status .ok { color: #8fd18b; margin-left: 10px; }
#status .clickable { cursor: pointer; }
#status .clickable:hover { text-decoration: underline; }
button { display: inline-block; padding: 5px 10px 6px 10px; margin-right: 6px; background-color: #3a3027; border: 1px #6c5838; border-radius: 4px; color: #ece3d0; cursor: pointer; white-space: nowrap; }
button:hover { background-color: #4d3f30; border-color: #a7864e; }
button:active { background-color: #2a221b; }
button.on { background-color: #6b5530; border-color: #c89b4a; }
.sep { display: inline-block; width: 1px; height: 22px; background-color: #5c4a30; margin: 0 8px 0 2px; }
.label { margin-right: 6px; color: #b9ab90; }
select { display: inline-block; height: 26px; margin-right: 6px; background-color: #2a241f; border: 1px #6c5838; border-radius: 4px; color: #ece3d0; cursor: pointer; }
select#open { width: 260px; }
select#scale { width: 80px; }
select#bg { width: 120px; }
select selectvalue { display: block; height: 18px; padding: 4px 22px 0 8px; white-space: nowrap; overflow: hidden; }
select selectarrow { width: 18px; height: 24px; background-color: #3a3027; border-left: 1px #6c5838; }
select:hover selectarrow { background-color: #4d3f30; }
select selectbox { background-color: #241f1a; border: 1px #6c5838; margin-top: 2px; max-height: 480px; overflow-y: auto; padding: 2px 0; }
select selectbox option { display: block; padding: 4px 10px; white-space: nowrap; }
select selectbox option:hover { background-color: #4d3f30; }
select selectbox option.group { color: #c89b4a; padding-top: 8px; font-weight: bold; }
scrollbarvertical { width: 10px; margin-left: 2px; }
scrollbarvertical slidertrack { background-color: #1a1612; }
scrollbarvertical sliderbar { background-color: #6c5838; border-radius: 4px; min-height: 24px; }
scrollbarvertical sliderarrowdec, scrollbarvertical sliderarrowinc { width: 0; height: 0; }
.chip { display: inline-block; padding: 3px 4px 4px 9px; margin-right: 6px; background-color: #2a241f; border: 1px #5c4a30; border-radius: 10px; font-size: 13px; }
.chip button { padding: 0 6px 1px 6px; margin: 0 0 0 4px; border-radius: 8px; border-width: 0; background-color: transparent; color: #b9ab90; }
.chip button:hover { background-color: #6b3a30; color: #fff; }
#hint { color: #8d8270; }
)";

void Viewer::BuildToolbar()
{
	if (toolbar)
	{
		toolbar->Close();
		toolbar = nullptr;
	}

	std::string opts = "<option value=\"\" selected=\"selected\">Open a window...</option>";
	std::string last_group;
	for (const auto& ui : ListGameDocuments())
	{
		// Group by folder (e.g. themes/rose/game)
		std::string folder = ui.substr(0, ui.find_last_of('/'));
		std::string group = folder;
		const std::string low = GameFiles::Lower(folder);
		size_t p = low.find("ui/themes/");
		if (p != std::string::npos)
			group = folder.substr(p + 10);
		if (group != last_group)
		{
			opts += "<option class=\"group\" value=\"\" disabled=\"disabled\">" + Escape(group) + "</option>";
			last_group = group;
		}
		opts += "<option value=\"" + Escape(ui) + "\">" + Escape(FileName(ui)) + "</option>";
	}

	std::string scale_opts;
	for (int s : {75, 100, 125, 150, 175, 200})
		scale_opts += "<option value=\"" + std::to_string(s) + "\"" + (std::abs(scale * 100 - s) < 1 ? " selected=\"selected\"" : "") + ">" +
			std::to_string(s) + "%</option>";

	std::string bg_opts;
	for (auto [v, label] : std::vector<std::pair<const char*, const char*>>{
			 {"dark", "Dark"}, {"grey", "Grey"}, {"light", "Light"}, {"black", "Black"}, {"green", "Green screen"}, {"image", "Screenshot"}})
		bg_opts += std::string("<option value=\"") + v + "\"" + (background_mode == v ? " selected=\"selected\"" : "") + ">" + label + "</option>";

	std::string rml = R"(<rml><head><title>viewer-toolbar</title><style>)" + std::string(kToolbarStyle) + R"(</style></head>
<body id="viewer-toolbar">
<div id="bar">
	<select id="open">)" + opts + R"(</select>
	<button data-action="reload" title="F5">Reload</button>
	<button data-action="auto" class=")" + (auto_reload ? "on" : "") + R"(">Auto-reload )" + (auto_reload ? "on" : "off") + R"(</button>
	<span class="sep"/>
	<span class="label">Size</span><select id="scale">)" + scale_opts + R"(</select>
	<span class="label">Background</span><select id="bg">)" + bg_opts + R"(</select>
	<span class="sep"/>
	<button data-action="inspect" title="F8">Inspector</button>
	<button data-action="mock">Sample data</button>
	<button data-action="folder">Game folder</button>
</div>
<div id="status"><span id="windows"></span><span class="grow"></span><span id="state"></span></div>
</body></rml>)";

	toolbar = context->LoadDocumentFromMemory(rml, "[viewer toolbar]");
	if (!toolbar)
		return;
	toolbar->AddEventListener(Rml::EventId::Click, this);
	toolbar->AddEventListener(Rml::EventId::Change, this);
	if (toolbar_visible)
		toolbar->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	UpdateToolbarStatus();
}

void Viewer::SetToolbarVisible(bool visible)
{
	toolbar_visible = visible;
	if (!toolbar)
		return;
	if (visible)
	{
		toolbar->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
		toolbar->PullToFront();
	}
	else
		toolbar->Hide();
	SaveConfig();
}

void Viewer::UpdateToolbarStatus()
{
	if (!toolbar)
		return;
	if (auto* w = toolbar->GetElementById("windows"))
	{
		std::string chips;
		if (GameFiles::Root().empty() || !GameFiles::LooksLikeGameFolder(GameFiles::Root()))
			chips = "<span id=\"hint\">Pick your ROSE Online folder with the Game folder button (or drop a window's .html file here).</span>";
		else if (open_docs.empty())
			chips = "<span id=\"hint\">Open a window from the list. Edits you save show up here straight away.</span>";
		for (const auto& ui : open_docs)
			chips += "<span class=\"chip\">" + Escape(FileName(ui)) + "<button data-close=\"" + Escape(ui) + "\">x</button></span>";
		w->SetInnerRML(chips);
	}
	if (auto* s = toolbar->GetElementById("state"))
	{
		std::string html;
		if (!last_reload_time.empty())
			html += "Loaded " + last_reload_time;
		if (system.errors)
			html += "<span class=\"err clickable\" data-action=\"inspect\">" + std::to_string(system.errors) + (system.errors == 1 ? " error" : " errors") + "</span>";
		if (system.warnings)
			html += "<span class=\"warn clickable\" data-action=\"inspect\">" + std::to_string(system.warnings) + (system.warnings == 1 ? " warning" : " warnings") + "</span>";
		if (!system.errors && !system.warnings && !last_reload_time.empty())
			html += "<span class=\"ok\">no problems</span>";
		s->SetInnerRML(html);
		if (system.errors || system.warnings)
			s->SetAttribute("title", system.recent.empty() ? std::string() : system.recent.back());
	}
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

// ---------------------------------------------------------------- events

void Viewer::ProcessEvent(Rml::Event& event)
{
	Rml::Element* target = event.GetTargetElement();
	if (!target)
		return;

	if (event.GetId() == Rml::EventId::Change)
	{
		const Rml::String id = target->GetId();
		const Rml::String value = event.GetParameter<Rml::String>("value", "");
		if (id == "open" && !value.empty())
			pending.push_back([this, value] {
				OpenDocument(value);
				BuildToolbar(); // resets the list to "Open a window..."
			});
		else if (id == "scale" && !value.empty())
			pending.push_back([this, value] { SetScale(std::stoi(value) / 100.f); });
		else if (id == "bg" && !value.empty())
			pending.push_back([this, value] {
				background_mode = value;
				ApplyBackground();
				SaveConfig();
			});
		return;
	}

	// Clicks: walk up to the element carrying the action.
	for (Rml::Element* e = target; e; e = e->GetParentNode())
	{
		if (Rml::Variant* close = e->GetAttribute("data-close"))
		{
			const Rml::String path = close->Get<Rml::String>();
			pending.push_back([this, path] { CloseDocument(path); });
			return;
		}
		Rml::Variant* action = e->GetAttribute("data-action");
		if (!action)
			continue;
		const Rml::String a = action->Get<Rml::String>();
		pending.push_back([this, a] { RunAction(a); });
		return;
	}
}

void Viewer::RunAction(const std::string& a)
{
	{
		if (a == "reload")
			ReloadAll();
		else if (a == "auto")
		{
			auto_reload = !auto_reload;
			SaveConfig();
			BuildToolbar();
		}
		else if (a == "inspect")
		{
			debugger_visible = !Rml::Debugger::IsVisible();
			Rml::Debugger::SetVisible(debugger_visible);
		}
		else if (a == "mock")
		{
			std::error_code ec;
			fs::create_directories(mocks.Folder(), ec);
			if (hooks.open_in_explorer)
				hooks.open_in_explorer(mocks.Folder());
		}
		else if (a == "folder")
		{
			if (hooks.pick_folder)
			{
				fs::path f = hooks.pick_folder();
				if (!f.empty())
				{
					fs::path game = GameFiles::LooksLikeGameFolder(f) ? f : GameFiles::FindGameFolderAbove(f);
					if (game.empty())
						Rml::Log::Message(Rml::Log::LT_WARNING, "That folder has no Data\\ui\\themes in it. Pick the ROSE Online folder itself.");
					else
						SetGameFolder(game);
				}
			}
		}
	}
}

bool Viewer::OnKeyDown(Rml::Input::KeyIdentifier key, int modifiers)
{
	using namespace Rml::Input;
	const bool ctrl = (modifiers & KM_CTRL) != 0;
	switch (key)
	{
	case KI_F5: ReloadAll(); return true;
	case KI_F8:
		Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
		return true;
	case KI_F2: SetToolbarVisible(!toolbar_visible); return true;
	case KI_OEM_PLUS:
	case KI_ADD:
		if (ctrl)
		{
			SetScale(scale + 0.25f);
			return true;
		}
		break;
	case KI_OEM_MINUS:
	case KI_SUBTRACT:
		if (ctrl)
		{
			SetScale(scale - 0.25f);
			return true;
		}
		break;
	case KI_0:
		if (ctrl)
		{
			SetScale(1.f);
			return true;
		}
		break;
	default: break;
	}
	return false;
}

bool Viewer::OnMouseButton(int button, bool down, int modifiers)
{
	if (button != 0)
		return false;
	if (down && (modifiers & Rml::Input::KM_ALT))
	{
		Rml::Element* hover = context->GetHoverElement();
		Rml::ElementDocument* doc = hover ? hover->GetOwnerDocument() : nullptr;
		if (doc && doc != toolbar && doc != background)
		{
			bool is_game = false;
			for (auto& [p, d] : loaded)
				if (d == doc)
					is_game = true;
			if (is_game)
			{
				dragging = doc;
				drag_start_mouse = mouse;
				Rml::Vector2f off = doc->GetAbsoluteOffset(Rml::BoxArea::Border);
				drag_start_pos = {(int)off.x, (int)off.y};
				doc->PullToFront();
				if (toolbar)
					toolbar->PullToFront();
				return true;
			}
		}
	}
	if (!down && dragging)
	{
		dragging = nullptr;
		RememberPositions();
		SaveConfig();
		return true;
	}
	return false;
}

bool Viewer::OnMouseMove(int x, int y)
{
	mouse = {x, y};
	if (!dragging)
		return false;
	dragging->SetProperty(Rml::PropertyId::Left, Rml::Property((float)(drag_start_pos.x + x - drag_start_mouse.x), Rml::Unit::PX));
	dragging->SetProperty(Rml::PropertyId::Top, Rml::Property((float)(drag_start_pos.y + y - drag_start_mouse.y), Rml::Unit::PX));
	return true;
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
		const std::string ext = GameFiles::Lower(p.extension().string());
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

void Viewer::OnResize(Rml::Vector2i /*size*/) {}
