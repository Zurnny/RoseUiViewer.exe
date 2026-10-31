#pragma once
// Runs the game's UI windows (RmlUi + Lua) outside the game and reloads them when their files change.
#include "GameFiles.h"
#include "MockData.h"
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
class RenderInterface;
} // namespace Rml

// Things only the windowed app can do.
struct PlatformHooks {
	std::function<fs::path()> pick_folder;                 // ask the user for the ROSE Online folder
	std::function<void(const fs::path&)> open_in_explorer; // show a folder or file in Explorer
	std::function<void(const std::string&)> set_title;
};

struct LogEntry {
	Rml::Log::Type type;
	std::string text;
	std::string time;
};

// Wraps the platform's system interface to keep the log.
class ViewerSystemInterface : public Rml::SystemInterface {
public:
	Rml::SystemInterface* base = nullptr;
	int errors = 0, warnings = 0;
	std::deque<LogEntry> log;
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

	double GetElapsedTime() override;
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
	void SetMouseCursor(const Rml::String& name) override
	{
		if (base)
			base->SetMouseCursor(name);
	}
	void SetClipboardText(const Rml::String& t) override
	{
		if (base)
			base->SetClipboardText(t);
	}
	void GetClipboardText(Rml::String& t) override
	{
		if (base)
			base->GetClipboardText(t);
	}
	void JoinPath(Rml::String& out, const Rml::String& doc_path, const Rml::String& path) override;
};

// The files a window is made of (UI paths, e.g. "Data/ui/themes/rose/game/hud.html").
struct WindowFiles {
	std::string document;
	std::vector<std::string> stylesheets, templates, scripts;
};

class Viewer {
public:
	bool Init(const fs::path& app_dir, Rml::RenderInterface* renderer, Rml::SystemInterface* platform_system, PlatformHooks hooks,
		Rml::Vector2i window_size, float content_scale);
	void Shutdown();

	Rml::Context* GetContext() const { return context; }
	const fs::path& AppDir() const { return app_dir; }
	const PlatformHooks& Hooks() const { return hooks; }

	// Once per frame.
	void Update();
	void Render();

	bool OnKeyDown(Rml::Input::KeyIdentifier key, int modifiers);
	void OnDrop(const std::vector<fs::path>& paths);

	// Game folder
	bool HasGameFolder() const;
	bool SetGameFolder(const fs::path& folder);
	void PickGameFolder();

	// Windows
	const std::vector<std::string>& ListGameDocuments(); // every window file in the theme (cached)
	void RefreshDocumentList() { doc_list_dirty = true; }
	const std::vector<std::string>& OpenDocuments() const { return open_docs; }
	void OpenDocument(const std::string& ui_path);
	void CloseDocument(const std::string& ui_path);
	Rml::ElementDocument* LoadedDocument(const std::string& ui_path) const;
	std::string PathOf(Rml::ElementDocument* doc) const; // "" for viewer documents
	const WindowFiles* FilesOf(const std::string& ui_path) const;
	void SetDocumentPosition(const std::string& ui_path, int x, int y);
	void BringToFront(Rml::ElementDocument* doc);

	// Reloading
	void ReloadAll();
	void RequestReload() { reload_requested = true; }
	void NoteOwnWrite(); // the viewer wrote a file itself; don't treat it as an outside change
	bool auto_reload = true;
	const std::string& LastReloadTime() const { return last_reload_time; }
	std::function<void()> on_reloaded; // the editor re-finds its selection here

	// View
	float Scale() const { return scale; }
	void SetScale(float scale);
	float DpRatio() const;
	const std::string& Background() const { return background_mode; }
	void SetBackground(const std::string& mode);
	void SetViewArea(Rml::Vector2f pos, Rml::Vector2f size) { view_pos = pos, view_size = size; }

	// Data
	MockModels& Mocks() { return mocks; }
	ViewerSystemInterface& System() { return system; }
	nlohmann::json& EditorSettings() { return editor_settings; }
	void SaveConfig();

private:
	void LoadConfig();
	void LoadFonts();
	void ApplyBackground();
	void RememberPositions();
	uint64_t Fingerprint() const;
	void UpdateTitle();

	fs::path app_dir;
	PlatformHooks hooks;
	ViewerSystemInterface system;
	GameFileInterface files;
	MockModels mocks;

	Rml::Context* context = nullptr;
	Rml::ElementDocument* background = nullptr;
	std::map<std::string, Rml::ElementDocument*> loaded; // ui path -> document
	std::map<std::string, WindowFiles> window_files;

	std::vector<std::string> open_docs;
	std::vector<std::string> doc_list;
	bool doc_list_dirty = true;
	std::map<std::string, Rml::Vector2i> positions;
	float scale = 1.0f;
	float content_scale = 1.0f;
	std::string background_mode = "dark";
	nlohmann::json editor_settings = nlohmann::json::object();
	Rml::Vector2f view_pos{0, 0}, view_size{0, 0};

	uint64_t last_fingerprint = 0;
	std::chrono::steady_clock::time_point last_check, change_seen;
	bool change_pending = false;
	bool reload_requested = false;
	std::string last_reload_time;
};
