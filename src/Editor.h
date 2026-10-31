#pragma once
// The editing side of ROSE UI Viewer: panels (Dear ImGui) for picking elements on screen,
// moving/resizing them, editing their styles, the window's code, and the sample data.
#include "SourceEdit.h"
#include "TextEditor.h"
#include "Updater.h"
#include "Viewer.h"
#include <RmlUi/Core/ObserverPtr.h>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct ImFont;

class Editor {
public:
	// Call after ImGui is created and its renderer backend is set up.
	void Init(Viewer* viewer, float ui_scale);
	void Shutdown();

	// Builds the UI. Call between ImGui::NewFrame() and ImGui::Render().
	void Frame();

	// True when the RmlUi windows shouldn't get mouse input (select mode, or the mouse is over a panel).
	bool BlocksCanvasMouse() const;
	// True while a text field or the code editor has the keyboard.
	bool WantsKeyboard() const;

	// Asked before the app closes; false = wait (a "save changes?" box is showing).
	bool ConfirmClose();

	// Updates: the app owns the updater; the editor shows its state and the Restart button.
	void SetUpdater(Updater* u) { updater = u; }
	bool WantsRestart() const { return restart_requested; }

	// Testing hooks (headless tool)
	bool SelectAt(float x, float y);
	bool CommitStyle(const std::string& property, const std::string& value);
	void SetSelectMode(bool on) { select_mode = on; }
	void SetTarget(const std::string& selector) { target_selector = selector; }
	bool SelectId(const std::string& id);
	void FocusPanel(const std::string& name) { focus_panel = name; }
	std::string SelectedBox() const; // "x,y,w,h" in px
	std::vector<std::string> DescribeStackAt(float x, float y) const;
	bool CycleAt(float x, float y); // Alt+click
	void OpenCodeFor(const std::string& ui_path);

private:
	struct CodeTab {
		std::string ui;   // UI path
		fs::path real;
		std::unique_ptr<TextEditor> editor;
		std::string saved; // text on disk
		bool dirty = false;
		bool applied = true; // the preview shows the current text
		std::chrono::steady_clock::time_point last_edit;
		std::chrono::steady_clock::time_point last_disk_check;
	};
	struct UndoStep {
		fs::path real;
		std::string before, after;
		std::string label;
	};
	enum class Drag { None, Move, Resize, MoveWindow };

	// Panels
	void DrawMenuBar();
	void DrawToolbar();
	void DrawElements();
	void DrawProperties();
	void DrawCode();
	void DrawSampleData();
	void DrawLog();
	void DrawOverlay();
	void DrawPopups();
	void SetupLayout(unsigned int dockspace);

	// Canvas (select mode, dragging)
	void HandleCanvas();
	Rml::Element* PickAt(float x, float y) const;
	std::vector<Rml::Element*> StackAt(float x, float y) const; // everything under the point, top first
	bool IsGameElement(Rml::Element* e) const;
	void Select(Rml::Element* e);
	void OnReloaded();
	int HandleAt(float x, float y) const; // -1 none, 0..7 resize handles, 8 = inside
	void FinishDrag();
	void Nudge(int dx, int dy);

	// Saving changes
	std::vector<std::string> SelectorChoices(Rml::Element* e) const;
	std::string TargetSelector(Rml::Element* e);
	bool CommitDecls(const SourceEdit::Decls& decls, const std::string& label);
	bool CommitDeclsAtSource(const std::string& source_path, int line, const SourceEdit::Decls& decls, const std::string& label);
	bool SetElementAttribute(Rml::Element* e, const std::string& name, const std::string& value, bool remove = false);
	bool ReadForEdit(const fs::path& real, std::string& text);
	void WriteSource(const fs::path& real, const std::string& text, const std::string& label, bool record_undo = true);
	void Undo();
	void Redo();

	// Code panel helpers
	CodeTab* FindTab(const fs::path& real);
	CodeTab& OpenTab(const std::string& ui);
	void GoToSource(const std::string& ui, int line, int col, int select_len);
	void SaveTab(CodeTab& tab);
	void SaveAllTabs();
	bool AnyDirty() const;
	void UpdateTabs();

	// Element info
	bool SourceOf(Rml::Element* e, int& file_index, size_t& offset) const;
	std::string Describe(Rml::Element* e) const;
	std::string ActiveWindow() const;

	Viewer* viewer = nullptr;
	Updater* updater = nullptr;
	bool restart_requested = false;
	bool update_failure_shown = false;
	bool manual_check = false;
	float ui_scale = 1.f;
	ImFont* font_ui = nullptr;
	ImFont* font_mono = nullptr;
	std::string ini_path;
	bool layout_built = false;

	// Panels shown
	bool show_elements = true, show_properties = true, show_code = true, show_data = true, show_log = true;
	bool show_help = false;

	bool select_mode = false;
	Rml::ObserverPtr<Rml::Element> selected, hovered;
	std::string sel_doc, sel_src;       // to find the selection again after a reload
	std::vector<int> sel_path;
	std::string target_selector;        // "Apply to" choice for the selected element
	std::string active_window;

	// Dragging
	Drag drag = Drag::None;
	int drag_handle = -1;
	float drag_x0 = 0, drag_y0 = 0;
	bool drag_moved = false;
	float base_left = 0, base_top = 0, base_w = 0, base_h = 0;
	bool use_right = false, use_bottom = false, make_relative = false;
	float cur_left = 0, cur_top = 0, cur_w = 0, cur_h = 0;
	Rml::Vector2f window_drag_start;
	Rml::ElementDocument* drag_window = nullptr;

	// Nudging with the arrow keys
	int nudge_dx = 0, nudge_dy = 0;
	std::chrono::steady_clock::time_point nudge_time;

	// Element without an id: ask for one before saving
	bool want_id_popup = false;
	char id_buffer[64] = {};
	SourceEdit::Decls pending_decls;
	std::string pending_label;

	// Property panel edit buffers
	std::string new_prop_name, new_prop_value;

	std::vector<UndoStep> undo, redo;
	std::vector<CodeTab> tabs;
	std::string code_active; // ui path of the selected tab
	bool code_live = true;
	std::string goto_tab;     // switch to this tab next frame

	// Sample data
	std::set<std::string> data_dirty;
	std::chrono::steady_clock::time_point data_edit_time;

	// Log
	bool log_errors = true, log_warnings = true, log_info = false;
	std::string log_filter;

	std::string focus_panel;

	// Elements under the mouse (right-click list, Alt+click cycling)
	std::vector<Rml::ObserverPtr<Rml::Element>> stack_items;
	bool want_stack_popup = false, stack_popup_open = false;

	// Closing
	bool close_popup = false, close_allowed = false;
};
