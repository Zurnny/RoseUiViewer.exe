#pragma once
#include "GameFiles.h"
#include "MockData.h"
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>
#include <chrono>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
class RenderInterface;
} // namespace Rml

// Things only the windowed app can do.
struct PlatformHooks {
	std::function<fs::path()> pick_folder;                  // ask the user for the ROSE Online folder
	std::function<void(const fs::path&)> open_in_explorer;  // show a folder or file in Explorer
	std::function<void(const std::string&)> set_title;
};

// Wraps the platform's system interface to count warnings/errors and keep a short log.
class ViewerSystemInterface : public Rml::SystemInterface {
public:
	Rml::SystemInterface* base = nullptr;
	int errors = 0, warnings = 0;
	std::vector<std::string> recent;
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

class Viewer : public Rml::EventListener {
public:
	bool Init(const fs::path& app_dir, Rml::RenderInterface* renderer, Rml::SystemInterface* platform_system, PlatformHooks hooks,
		Rml::Vector2i window_size, float content_scale);
	void Shutdown();

	Rml::Context* GetContext() const { return context; }

	// Call once per frame before rendering.
	void Update();
	void Render();

	// Input hooks; return true when the viewer used the event (don't pass it on).
	bool OnKeyDown(Rml::Input::KeyIdentifier key, int modifiers);
	bool OnMouseButton(int button, bool down, int modifiers);
	bool OnMouseMove(int x, int y);
	void OnDrop(const std::vector<fs::path>& paths);
	void OnResize(Rml::Vector2i size);

	bool SetGameFolder(const fs::path& folder);
	void OpenDocument(const std::string& ui_path);
	void CloseDocument(const std::string& ui_path);
	void ReloadAll();
	void SetScale(float scale);

	const std::vector<std::string>& OpenDocuments() const { return open_docs; }
	void SetToolbarVisible(bool visible);

	// Headless/testing helpers
	void SetDocumentPosition(const std::string& ui_path, int x, int y);

private:
	void ProcessEvent(Rml::Event& event) override;
	void RunAction(const std::string& action);

	void LoadConfig();
	void SaveConfig();
	void LoadFonts();
	void BuildToolbar();
	void UpdateToolbarStatus();
	void ApplyBackground();
	std::vector<std::string> ListGameDocuments() const;
	void RememberPositions();
	uint64_t Fingerprint() const;
	void UpdateTitle();

	fs::path app_dir;
	PlatformHooks hooks;
	ViewerSystemInterface system;
	GameFileInterface files;
	MockModels mocks;

	Rml::Context* context = nullptr;
	Rml::ElementDocument* toolbar = nullptr;
	Rml::ElementDocument* background = nullptr;
	std::map<std::string, Rml::ElementDocument*> loaded; // ui path -> document

	std::vector<std::string> open_docs;
	std::map<std::string, Rml::Vector2i> positions;
	float scale = 1.0f;
	float content_scale = 1.0f;
	std::string background_mode = "dark";
	bool auto_reload = true;
	bool toolbar_visible = true;
	bool debugger_visible = false;

	uint64_t last_fingerprint = 0;
	std::chrono::steady_clock::time_point last_check, change_seen;
	bool change_pending = false;
	bool reload_requested = false;
	std::vector<std::function<void()>> pending; // toolbar actions, run between frames (not inside RmlUi's event dispatch)
	std::string last_reload_time;

	// Alt+drag moves any window
	Rml::ElementDocument* dragging = nullptr;
	Rml::Vector2i drag_start_mouse, drag_start_pos, mouse;
};
