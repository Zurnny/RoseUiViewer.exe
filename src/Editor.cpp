#include "Editor.h"
#include "EditorLanguages.h"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

extern const unsigned char g_font_regular[];
extern const size_t g_font_regular_size;
extern const unsigned char g_font_mono[];
extern const size_t g_font_mono_size;

using clk = std::chrono::steady_clock;
using nlohmann::json;

namespace {

const ImVec4 kBrass(0.84f, 0.66f, 0.33f, 1.f);
const ImVec4 kDim(0.62f, 0.60f, 0.56f, 1.f);
const ImVec4 kErr(1.f, 0.48f, 0.40f, 1.f);
const ImVec4 kWarn(0.95f, 0.75f, 0.35f, 1.f);
const ImVec4 kOk(0.56f, 0.82f, 0.55f, 1.f);

std::string FileName(const std::string& p)
{
	size_t s = p.find_last_of("/\\");
	return s == std::string::npos ? p : p.substr(s + 1);
}

std::string Fmt(float v)
{
	char b[32];
	if (std::fabs(v - std::round(v)) < 0.01f)
		snprintf(b, sizeof(b), "%d", (int)std::round(v));
	else
		snprintf(b, sizeof(b), "%.1f", v);
	return b;
}

const char* UnitSuffix(Rml::Unit u)
{
	switch (u)
	{
	case Rml::Unit::PX: return "px";
	case Rml::Unit::DP: return "dp";
	case Rml::Unit::PERCENT: return "%";
	case Rml::Unit::EM: return "em";
	case Rml::Unit::REM: return "rem";
	case Rml::Unit::NUMBER: return "";
	case Rml::Unit::DEG: return "deg";
	case Rml::Unit::VW: return "vw";
	case Rml::Unit::VH: return "vh";
	default: return nullptr;
	}
}

struct EnumProp {
	const char* name;
	std::vector<const char*> options;
};
const std::vector<EnumProp>& EnumProps()
{
	static std::vector<EnumProp> v = {
		{"position", {"static", "relative", "absolute", "fixed"}},
		{"display", {"none", "block", "inline", "inline-block", "flex", "inline-flex", "table", "table-row", "table-cell"}},
		{"visibility", {"visible", "hidden"}},
		{"overflow", {"visible", "hidden", "auto", "scroll"}},
		{"text-align", {"left", "right", "center", "justify"}},
		{"font-weight", {"normal", "bold", "300", "400", "500", "600", "700", "800", "900"}},
		{"font-style", {"normal", "italic"}},
		{"white-space", {"normal", "pre", "nowrap", "pre-wrap", "pre-line"}},
		{"box-sizing", {"content-box", "border-box"}},
		{"flex-direction", {"row", "row-reverse", "column", "column-reverse"}},
		{"flex-wrap", {"nowrap", "wrap", "wrap-reverse"}},
		{"justify-content", {"flex-start", "flex-end", "center", "space-between", "space-around", "space-evenly"}},
		{"align-items", {"flex-start", "flex-end", "center", "baseline", "stretch"}},
		{"text-decoration", {"none", "underline", "overline", "line-through"}},
		{"pointer-events", {"auto", "none"}},
	};
	return v;
}
const EnumProp* FindEnum(const std::string& n)
{
	for (auto& e : EnumProps())
		if (n == e.name)
			return &e;
	return nullptr;
}

bool IsColorProp(const std::string& n) { return n == "color" || n == "background-color" || n == "image-color" || n == "border-color"; }

// Shorthands shown as one row; their value is read from a longhand.
const char* LonghandFor(const std::string& n)
{
	if (n == "margin")
		return "margin-top";
	if (n == "padding")
		return "padding-top";
	if (n == "border-width")
		return "border-top-width";
	if (n == "border-color")
		return "border-top-color";
	if (n == "border-radius")
		return "border-top-left-radius";
	return nullptr;
}

struct Group {
	const char* title;
	std::vector<const char*> props;
};
const std::vector<Group>& Groups()
{
	static std::vector<Group> g = {
		{"Position", {"position", "left", "top", "right", "bottom", "z-index"}},
		{"Size", {"width", "height", "min-width", "min-height", "max-width", "max-height", "box-sizing"}},
		{"Spacing", {"margin", "margin-top", "margin-right", "margin-bottom", "margin-left", "padding", "padding-top", "padding-right", "padding-bottom",
						"padding-left"}},
		{"Text", {"color", "font-family", "font-size", "font-weight", "font-style", "text-align", "line-height", "white-space", "font-effect",
					 "text-decoration"}},
		{"Look", {"background-color", "decorator", "opacity", "image-color", "border-width", "border-color", "border-radius"}},
		{"Layout", {"display", "visibility", "overflow", "flex-direction", "flex-wrap", "justify-content", "align-items", "flex", "pointer-events"}},
	};
	return g;
}

std::string ColorText(const Rml::Colourb& c)
{
	char b[48];
	if (c.alpha == 255)
		snprintf(b, sizeof(b), "#%02x%02x%02x", c.red, c.green, c.blue);
	else
		snprintf(b, sizeof(b), "rgba(%d, %d, %d, %d)", c.red, c.green, c.blue, c.alpha);
	return b;
}

bool ValidId(const std::string& s)
{
	if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_'))
		return false;
	for (char c : s)
		if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-'))
			return false;
	return true;
}

} // namespace

// ---------------------------------------------------------------- setup

void Editor::Init(Viewer* v, float scale)
{
	viewer = v;
	ui_scale = scale;
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
	ini_path = (viewer->AppDir() / "editor_layout.ini").u8string();
	io.IniFilename = ini_path.c_str();

	ImFontConfig cfg;
	cfg.FontDataOwnedByAtlas = false;
	font_ui = io.Fonts->AddFontFromMemoryTTF((void*)g_font_regular, (int)g_font_regular_size, 16.f * scale, &cfg);
	font_mono = io.Fonts->AddFontFromMemoryTTF((void*)g_font_mono, (int)g_font_mono_size, 15.f * scale, &cfg);

	ImGuiStyle& st = ImGui::GetStyle();
	ImGui::StyleColorsDark(&st);
	st.WindowRounding = 4;
	st.FrameRounding = 3;
	st.TabRounding = 3;
	st.GrabRounding = 3;
	st.WindowBorderSize = 1;
	st.FramePadding = ImVec2(7, 4);
	st.ItemSpacing = ImVec2(7, 5);
	ImVec4* c = st.Colors;
	c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.10f, 0.09f, 1.f);
	c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.09f, 0.08f, 1.f);
	c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.11f, 0.10f, 0.98f);
	c[ImGuiCol_Border] = ImVec4(0.36f, 0.29f, 0.19f, 0.8f);
	c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.15f, 0.13f, 1.f);
	c[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.22f, 0.17f, 1.f);
	c[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.26f, 0.18f, 1.f);
	c[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.11f, 0.10f, 1.f);
	c[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.16f, 0.11f, 1.f);
	c[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.12f, 0.10f, 1.f);
	c[ImGuiCol_Button] = ImVec4(0.23f, 0.19f, 0.15f, 1.f);
	c[ImGuiCol_ButtonHovered] = ImVec4(0.33f, 0.27f, 0.19f, 1.f);
	c[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.35f, 0.20f, 1.f);
	c[ImGuiCol_Header] = ImVec4(0.30f, 0.24f, 0.16f, 1.f);
	c[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.30f, 0.19f, 1.f);
	c[ImGuiCol_HeaderActive] = ImVec4(0.48f, 0.37f, 0.21f, 1.f);
	c[ImGuiCol_Tab] = ImVec4(0.17f, 0.15f, 0.12f, 1.f);
	c[ImGuiCol_TabHovered] = ImVec4(0.40f, 0.31f, 0.19f, 1.f);
	c[ImGuiCol_TabSelected] = ImVec4(0.32f, 0.25f, 0.16f, 1.f);
	c[ImGuiCol_TabDimmed] = ImVec4(0.14f, 0.13f, 0.11f, 1.f);
	c[ImGuiCol_TabDimmedSelected] = ImVec4(0.24f, 0.19f, 0.13f, 1.f);
	c[ImGuiCol_CheckMark] = kBrass;
	c[ImGuiCol_SliderGrab] = kBrass;
	c[ImGuiCol_SliderGrabActive] = ImVec4(1.f, 0.8f, 0.45f, 1.f);
	c[ImGuiCol_DockingPreview] = ImVec4(0.84f, 0.66f, 0.33f, 0.5f);
	c[ImGuiCol_SeparatorHovered] = kBrass;
	c[ImGuiCol_ResizeGripHovered] = kBrass;
	c[ImGuiCol_TextSelectedBg] = ImVec4(0.6f, 0.45f, 0.2f, 0.45f);
	st.ScaleAllSizes(scale);

	auto& es = viewer->EditorSettings();
	show_elements = es.value("show_elements", true);
	show_properties = es.value("show_properties", true);
	show_code = es.value("show_code", true);
	show_data = es.value("show_data", true);
	show_log = es.value("show_log", true);
	code_live = es.value("code_live", true);
	select_mode = es.value("select_mode", false);

	viewer->on_reloaded = [this] { OnReloaded(); };
}

void Editor::Shutdown()
{
	auto& es = viewer->EditorSettings();
	es["show_elements"] = show_elements;
	es["show_properties"] = show_properties;
	es["show_code"] = show_code;
	es["show_data"] = show_data;
	es["show_log"] = show_log;
	es["code_live"] = code_live;
	es["select_mode"] = select_mode;
	es["layout_built"] = true;
	for (auto& t : tabs)
		GameFiles::ClearOverlay(t.real);
	viewer->on_reloaded = nullptr;
}

bool Editor::BlocksCanvasMouse() const
{
	const ImGuiIO& io = ImGui::GetIO();
	return io.WantCaptureMouse || select_mode || drag != Drag::None || io.KeyAlt;
}

bool Editor::WantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

// ---------------------------------------------------------------- frame

void Editor::Frame()
{
	ImGuiIO& io = ImGui::GetIO();
	ImGui::PushFont(font_ui, 0.f);

	DrawMenuBar();
	DrawToolbar();
	const ImGuiID dock = ImGui::DockSpaceOverViewport(ImGui::GetID("MainDock"), ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
	if (!layout_built)
	{
		layout_built = true;
		if (!viewer->EditorSettings().value("layout_built", false) || ImGui::DockBuilderGetNode(dock) == nullptr ||
			ImGui::DockBuilderGetNode(dock)->IsLeafNode())
			SetupLayout(dock);
	}
	if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dock))
	{
		const ImVec2 s = io.DisplayFramebufferScale;
		viewer->SetViewArea({central->Pos.x * s.x, central->Pos.y * s.y}, {central->Size.x * s.x, central->Size.y * s.y});
	}

	// Global shortcuts (not while typing)
	if (!io.WantTextInput)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
			viewer->ReloadAll();
		if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
			Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
		if (ImGui::IsKeyPressed(ImGuiKey_V, false) && !io.KeyCtrl)
			select_mode = !select_mode;
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			if (selected)
				Select(nullptr);
			else
				select_mode = false;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.KeyShift)
			Undo();
		if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))))
			Redo();
		if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)))
			viewer->SetScale(viewer->Scale() + 0.25f);
		if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)))
			viewer->SetScale(viewer->Scale() - 0.25f);
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false))
			viewer->SetScale(1.f);
		if (select_mode && selected && !io.KeyCtrl)
		{
			const int step = io.KeyShift ? 10 : 1;
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
				Nudge(-step, 0);
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
				Nudge(step, 0);
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				Nudge(0, -step);
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				Nudge(0, step);
		}
	}
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		SaveAllTabs();

	// Finish a nudge after a short pause.
	if ((nudge_dx || nudge_dy) && clk::now() - nudge_time > std::chrono::milliseconds(450))
		FinishDrag();

	HandleCanvas();
	DrawOverlay();

	if (show_elements)
		DrawElements();
	if (show_properties)
		DrawProperties();
	if (show_code)
		DrawCode();
	if (show_data)
		DrawSampleData();
	if (show_log)
		DrawLog();
	UpdateTabs();
	DrawPopups();
	if (!focus_panel.empty())
	{
		ImGui::SetWindowFocus(focus_panel.c_str());
		focus_panel.clear();
	}

	ImGui::PopFont();
}

void Editor::SetupLayout(ImGuiID dock)
{
	ImGui::DockBuilderRemoveNode(dock);
	ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
	ImGuiID center = dock, left, right, bottom, left_bottom;
	left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
	right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.40f, nullptr, &center);
	bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
	left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.60f, nullptr, &left);
	ImGui::DockBuilderDockWindow("Elements", left);
	ImGui::DockBuilderDockWindow("Properties", left_bottom);
	ImGui::DockBuilderDockWindow("Code", right);
	ImGui::DockBuilderDockWindow("Sample data", right);
	ImGui::DockBuilderDockWindow("Log", bottom);
	ImGui::DockBuilderFinish(dock);
	viewer->EditorSettings()["layout_built"] = true;
}

// ---------------------------------------------------------------- menu + toolbar

void Editor::DrawMenuBar()
{
	if (!ImGui::BeginMainMenuBar())
		return;
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Choose game folder..."))
			viewer->PickGameFolder();
		if (ImGui::BeginMenu("Open window", viewer->HasGameFolder()))
		{
			std::string group;
			for (const auto& ui : viewer->ListGameDocuments())
			{
				std::string folder = ui.substr(0, ui.find_last_of('/'));
				size_t p = GameFiles::Lower(folder).find("ui/themes/");
				std::string g = p == std::string::npos ? folder : folder.substr(p + 10);
				if (g != group)
				{
					ImGui::SeparatorText(g.c_str());
					group = g;
				}
				if (ImGui::MenuItem(FileName(ui).c_str()))
					viewer->OpenDocument(ui);
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Close all windows", nullptr, false, !viewer->OpenDocuments().empty()))
			for (auto ui : std::vector<std::string>(viewer->OpenDocuments()))
				viewer->CloseDocument(ui);
		ImGui::Separator();
		if (ImGui::MenuItem("Reload", "F5"))
			viewer->ReloadAll();
		if (ImGui::MenuItem("Save code", "Ctrl+S", false, AnyDirty()))
			SaveAllTabs();
		ImGui::Separator();
		if (ImGui::MenuItem("Open sample data folder") && viewer->Hooks().open_in_explorer)
		{
			std::error_code ec;
			fs::create_directories(viewer->Mocks().Folder(), ec);
			viewer->Hooks().open_in_explorer(viewer->Mocks().Folder());
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem(undo.empty() ? "Undo" : ("Undo " + undo.back().label).c_str(), "Ctrl+Z", false, !undo.empty()))
			Undo();
		if (ImGui::MenuItem(redo.empty() ? "Redo" : ("Redo " + redo.back().label).c_str(), "Ctrl+Y", false, !redo.empty()))
			Redo();
		ImGui::Separator();
		ImGui::MenuItem("Select mode", "V", &select_mode);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("View"))
	{
		ImGui::MenuItem("Elements", nullptr, &show_elements);
		ImGui::MenuItem("Properties", nullptr, &show_properties);
		ImGui::MenuItem("Code", nullptr, &show_code);
		ImGui::MenuItem("Sample data", nullptr, &show_data);
		ImGui::MenuItem("Log", nullptr, &show_log);
		ImGui::Separator();
		if (ImGui::MenuItem("Center all windows", nullptr, false, !viewer->OpenDocuments().empty()))
			viewer->CenterAll();
		ImGui::Separator();
		if (ImGui::MenuItem("RmlUi inspector", "F8", Rml::Debugger::IsVisible()))
			Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
		if (ImGui::MenuItem("Reset panel layout"))
		{
			show_elements = show_properties = show_code = show_data = show_log = true;
			layout_built = false;
			viewer->EditorSettings()["layout_built"] = false;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("Shortcuts"))
			show_help = true;
		ImGui::Separator();
		if (updater)
		{
			const bool unsupported = updater->GetState() == Updater::State::Unsupported;
			if (ImGui::MenuItem("Check for updates", nullptr, false, !unsupported))
			{
				manual_check = true;
				update_failure_shown = false;
				updater->CheckNow();
			}
			bool autoupd = viewer->EditorSettings().value("auto_update", true);
			if (ImGui::MenuItem("Update automatically", nullptr, &autoupd, !unsupported))
			{
				viewer->EditorSettings()["auto_update"] = autoupd;
				viewer->SaveConfig();
			}
			ImGui::TextDisabled("Version %s", updater->Version().c_str());
			if (unsupported)
				ImGui::TextDisabled("%s", updater->Message().c_str());
		}
		ImGui::EndMenu();
	}
	ImGui::EndMainMenuBar();
}

void Editor::DrawToolbar()
{
	ImGuiViewport* vp = ImGui::GetMainViewport();
	const float h = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;
	if (!ImGui::BeginViewportSideBar("##toolbar", vp, ImGuiDir_Up, h, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar * 0))
	{
		ImGui::End();
		return;
	}

	if (!viewer->HasGameFolder())
	{
		if (ImGui::Button("Choose your ROSE Online folder"))
			viewer->PickGameFolder();
		ImGui::SameLine();
		ImGui::TextColored(kDim, "the one that has Data in it (you can also drop it on this window)");
	}
	else
	{
		ImGui::SetNextItemWidth(220 * ui_scale);
		if (ImGui::BeginCombo("##open", "Open a window...", ImGuiComboFlags_HeightLarge))
		{
			std::string group;
			for (const auto& ui : viewer->ListGameDocuments())
			{
				std::string folder = ui.substr(0, ui.find_last_of('/'));
				size_t p = GameFiles::Lower(folder).find("ui/themes/");
				std::string g = p == std::string::npos ? folder : folder.substr(p + 10);
				if (g != group)
				{
					ImGui::SeparatorText(g.c_str());
					group = g;
				}
				if (ImGui::Selectable(FileName(ui).c_str()))
				{
					viewer->OpenDocument(ui);
					active_window = ui;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();

		// Mode
		if (ImGui::RadioButton("Interact", !select_mode))
			select_mode = false;
		ImGui::SetItemTooltip("Use the windows like in game: click buttons, hover tooltips.\nAlt + drag moves a whole window.");
		ImGui::SameLine();
		if (ImGui::RadioButton("Select  (V)", select_mode))
			select_mode = true;
		ImGui::SetItemTooltip("Click an element to edit it. Drag it to move, drag the handles to resize.\nArrow keys nudge it (Shift = 10).");
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		if (ImGui::Button("Reload"))
			viewer->ReloadAll();
		ImGui::SameLine();
		ImGui::Checkbox("Auto", &viewer->auto_reload);
		ImGui::SetItemTooltip("Reload by itself when a file changes");
		ImGui::SameLine();
		ImGui::BeginDisabled(ActiveWindow().empty());
		if (ImGui::Button("Center"))
		{
			viewer->CenterDocument(ActiveWindow());
			viewer->SaveConfig();
		}
		ImGui::SetItemTooltip("Center %s in the preview.\nView > Center all windows does every open window.", FileName(ActiveWindow()).c_str());
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(undo.empty());
		if (ImGui::ArrowButton("##undo", ImGuiDir_Left))
			Undo();
		ImGui::SetItemTooltip("%s", undo.empty() ? "Undo" : ("Undo " + undo.back().label).c_str());
		ImGui::EndDisabled();
		ImGui::SameLine(0, 2);
		ImGui::BeginDisabled(redo.empty());
		if (ImGui::ArrowButton("##redo", ImGuiDir_Right))
			Redo();
		ImGui::SetItemTooltip("%s", redo.empty() ? "Redo" : ("Redo " + redo.back().label).c_str());
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80 * ui_scale);
		char scale_label[16];
		snprintf(scale_label, sizeof(scale_label), "%d%%", (int)std::round(viewer->Scale() * 100));
		if (ImGui::BeginCombo("##scale", scale_label))
		{
			for (int s : {75, 100, 125, 150, 175, 200})
				if (ImGui::Selectable((std::to_string(s) + "%").c_str(), std::abs(viewer->Scale() * 100 - s) < 1))
					viewer->SetScale(s / 100.f);
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Interface size (Ctrl + / Ctrl -)");
		ImGui::SameLine();
		static const std::pair<const char*, const char*> bgs[] = {
			{"dark", "Dark"}, {"grey", "Grey"}, {"light", "Light"}, {"black", "Black"}, {"green", "Green screen"}, {"image", "Screenshot"}};
		const char* bg_label = "Dark";
		for (auto& b : bgs)
			if (viewer->Background() == b.first)
				bg_label = b.second;
		ImGui::SetNextItemWidth(120 * ui_scale);
		if (ImGui::BeginCombo("##bg", bg_label))
		{
			for (auto& b : bgs)
				if (ImGui::Selectable(b.second, viewer->Background() == b.first))
					viewer->SetBackground(b.first);
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Background.\nScreenshot: put background.png next to the viewer");

		// Open windows
		for (const auto& ui : std::vector<std::string>(viewer->OpenDocuments()))
		{
			ImGui::SameLine();
			ImGui::PushID(ui.c_str());
			const bool active = ui == ActiveWindow();
			if (active)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Header]);
			if (ImGui::SmallButton(FileName(ui).c_str()))
			{
				active_window = ui;
				viewer->BringToFront(viewer->LoadedDocument(ui));
			}
			if (ImGui::BeginPopupContextItem("chip"))
			{
				if (ImGui::MenuItem("Center on screen"))
				{
					viewer->CenterDocument(ui);
					viewer->SaveConfig();
				}
				if (ImGui::MenuItem("Bring to front"))
					viewer->BringToFront(viewer->LoadedDocument(ui));
				if (ImGui::MenuItem("Close"))
					viewer->CloseDocument(ui);
				ImGui::EndPopup();
			}
			ImGui::SetItemTooltip("Click: bring to front. Right-click: center, close.");
			if (active)
				ImGui::PopStyleColor();
			ImGui::SameLine(0, 1);
			if (ImGui::SmallButton("x"))
				viewer->CloseDocument(ui);
			ImGui::SetItemTooltip("Close %s", FileName(ui).c_str());
			ImGui::PopID();
		}
	}

	// Update status
	if (updater)
	{
		const auto st = updater->GetState();
		if (st == Updater::State::Ready)
		{
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.45f, 0.22f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.56f, 0.28f, 1.f));
			if (ImGui::SmallButton(("Update " + updater->LatestVersion() + " ready - Restart").c_str()))
				restart_requested = true;
			ImGui::PopStyleColor(2);
			ImGui::SetItemTooltip("The new version is in place. Restart now, or it opens next time you start the viewer.");
		}
		else if (st == Updater::State::Downloading)
		{
			ImGui::SameLine();
			ImGui::TextColored(kDim, "Updating %d%%", (int)(updater->Progress() * 100));
		}
		else if (st == Updater::State::Checking && manual_check)
		{
			ImGui::SameLine();
			ImGui::TextColored(kDim, "Checking for updates...");
		}
		else if (st == Updater::State::UpToDate && manual_check)
		{
			Rml::Log::Message(Rml::Log::LT_INFO, "ROSE UI Viewer %s is up to date.", updater->Version().c_str());
			show_log = log_info = true;
			manual_check = false;
		}
		else if (st == Updater::State::Failed && !update_failure_shown)
		{
			update_failure_shown = true;
			Rml::Log::Message(manual_check ? Rml::Log::LT_WARNING : Rml::Log::LT_INFO, "%s", updater->Message().c_str());
			manual_check = false;
		}
	}

	// Status, right-aligned
	auto& sys = viewer->System();
	std::string status;
	if (!viewer->LastReloadTime().empty())
		status = "Loaded " + viewer->LastReloadTime();
	const std::string errs = sys.errors ? std::to_string(sys.errors) + (sys.errors == 1 ? " error" : " errors") : "";
	const std::string warns = sys.warnings ? std::to_string(sys.warnings) + (sys.warnings == 1 ? " warning" : " warnings") : "";
	float w = ImGui::CalcTextSize(status.c_str()).x + ImGui::CalcTextSize(errs.c_str()).x + ImGui::CalcTextSize(warns.c_str()).x + 30 * ui_scale;
	ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - w));
	ImGui::TextDisabled("%s", status.c_str());
	if (!errs.empty())
	{
		ImGui::SameLine();
		ImGui::TextColored(kErr, "%s", errs.c_str());
		if (ImGui::IsItemClicked())
			show_log = log_errors = true;
	}
	if (!warns.empty())
	{
		ImGui::SameLine();
		ImGui::TextColored(kWarn, "%s", warns.c_str());
		if (ImGui::IsItemClicked())
			show_log = log_warnings = true;
	}
	ImGui::End();
}

// ---------------------------------------------------------------- element helpers

std::string Editor::ActiveWindow() const
{
	const auto& open = viewer->OpenDocuments();
	if (!active_window.empty() && std::find(open.begin(), open.end(), active_window) != open.end())
		return active_window;
	if (!sel_doc.empty() && std::find(open.begin(), open.end(), sel_doc) != open.end())
		return sel_doc;
	return open.empty() ? std::string() : open.back();
}

bool Editor::IsGameElement(Rml::Element* e) const
{
	if (!e)
		return false;
	Rml::ElementDocument* d = e->GetOwnerDocument();
	return d && !viewer->PathOf(d).empty();
}

std::string Editor::Describe(Rml::Element* e) const
{
	if (!e)
		return "";
	std::string s = e->GetTagName();
	const std::string id = e->GetId();
	if (!id.empty())
		s += "#" + id;
	std::string cls = e->GetClassNames();
	size_t i = 0;
	while (i < cls.size())
	{
		size_t j = cls.find(' ', i);
		std::string c = cls.substr(i, j == std::string::npos ? std::string::npos : j - i);
		if (!c.empty() && c != "viewer-has-item")
			s += "." + c;
		if (j == std::string::npos)
			break;
		i = j + 1;
	}
	return s;
}

bool Editor::SourceOf(Rml::Element* e, int& file_index, size_t& offset) const
{
	if (!e)
		return false;
	const Rml::String src = e->GetAttribute<Rml::String>("viewer-src", "");
	size_t colon = src.find(':');
	if (colon == std::string::npos)
		return false;
	try
	{
		file_index = std::stoi(src.substr(0, colon));
		offset = (size_t)std::stoull(src.substr(colon + 1));
	} catch (...)
	{
		return false;
	}
	return true;
}

static Rml::Element* PickIn(Rml::Element* e, float x, float y)
{
	if (!e->IsVisible())
		return nullptr;
	if (e->GetTagName() == "#text")
		return nullptr;
	for (int i = e->GetNumChildren() - 1; i >= 0; i--)
		if (Rml::Element* hit = PickIn(e->GetChild(i), x, y))
			return hit;
	const Rml::Vector2f p = e->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f s = e->GetBox().GetSize(Rml::BoxArea::Border);
	if (s.x > 0 && s.y > 0 && x >= p.x && y >= p.y && x < p.x + s.x && y < p.y + s.y)
		return e;
	return nullptr;
}

Rml::Element* Editor::PickAt(float x, float y) const
{
	// Find the top-most window under the point first (RmlUi knows the stacking), then the deepest element in it,
	// including decorations with pointer-events: none that RmlUi itself would skip.
	Rml::Context* ctx = viewer->GetContext();
	Rml::Element* top = ctx->GetElementAtPoint({x, y});
	Rml::ElementDocument* doc = top ? top->GetOwnerDocument() : nullptr;
	if (!doc || viewer->PathOf(doc).empty())
	{
		doc = nullptr;
		for (int i = ctx->GetNumDocuments() - 1; i >= 0 && !doc; i--)
		{
			Rml::ElementDocument* d = ctx->GetDocument(i);
			if (!viewer->PathOf(d).empty() && d->IsVisible() && PickIn(d, x, y))
				doc = d;
		}
	}
	if (!doc)
		return nullptr;
	Rml::Element* hit = PickIn(doc, x, y);
	return hit ? hit : doc;
}

void Editor::Select(Rml::Element* e)
{
	if (!e || !IsGameElement(e))
	{
		selected.reset();
		sel_doc.clear();
		sel_src.clear();
		sel_path.clear();
		return;
	}
	if (e != selected.get())
		target_selector.clear();
	selected = e->GetObserverPtr();
	sel_doc = viewer->PathOf(e->GetOwnerDocument());
	active_window = sel_doc;
	sel_src = e->GetAttribute<Rml::String>("viewer-src", "");
	sel_path.clear();
	for (Rml::Element* c = e; c && c != c->GetOwnerDocument(); c = c->GetParentNode())
	{
		Rml::Element* p = c->GetParentNode();
		if (!p)
			break;
		int idx = 0;
		for (int i = 0; i < p->GetNumChildren(); i++)
			if (p->GetChild(i) == c)
				idx = i;
		sel_path.insert(sel_path.begin(), idx);
	}
}

static Rml::Element* FindBySrc(Rml::Element* e, const std::string& src)
{
	if (e->GetAttribute<Rml::String>("viewer-src", "") == src)
		return e;
	for (int i = 0; i < e->GetNumChildren(); i++)
		if (Rml::Element* f = FindBySrc(e->GetChild(i), src))
			return f;
	return nullptr;
}

void Editor::OnReloaded()
{
	hovered.reset();
	drag = Drag::None;
	if (sel_doc.empty())
		return;
	Rml::ElementDocument* doc = viewer->LoadedDocument(sel_doc);
	Rml::Element* found = nullptr;
	if (doc)
	{
		if (!sel_src.empty())
			found = FindBySrc(doc, sel_src);
		if (!found)
		{
			Rml::Element* c = doc;
			for (int idx : sel_path)
			{
				if (!c || idx >= c->GetNumChildren())
				{
					c = nullptr;
					break;
				}
				c = c->GetChild(idx);
			}
			found = c;
		}
	}
	const std::string keep_target = target_selector;
	if (found)
	{
		selected = found->GetObserverPtr();
		sel_src = found->GetAttribute<Rml::String>("viewer-src", "");
		target_selector = keep_target;
	}
	else
		Select(nullptr);
}

// ---------------------------------------------------------------- canvas

int Editor::HandleAt(float x, float y) const
{
	Rml::Element* e = selected.get();
	if (!e)
		return -1;
	const Rml::Vector2f p = e->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f s = e->GetBox().GetSize(Rml::BoxArea::Border);
	const float r = 6.f * ui_scale * ImGui::GetIO().DisplayFramebufferScale.x;
	const float hx[8] = {p.x, p.x + s.x / 2, p.x + s.x, p.x + s.x, p.x + s.x, p.x + s.x / 2, p.x, p.x};
	const float hy[8] = {p.y, p.y, p.y, p.y + s.y / 2, p.y + s.y, p.y + s.y, p.y + s.y, p.y + s.y / 2};
	for (int i = 0; i < 8; i++)
		if (std::fabs(x - hx[i]) <= r && std::fabs(y - hy[i]) <= r)
			return i;
	if (x >= p.x && y >= p.y && x < p.x + s.x && y < p.y + s.y)
		return 8;
	return -1;
}

void Editor::HandleCanvas()
{
	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 fbs = io.DisplayFramebufferScale;
	const float mx = io.MousePos.x * fbs.x, my = io.MousePos.y * fbs.y;
	const bool over_canvas = !io.WantCaptureMouse || drag != Drag::None;

	// Interact mode: Alt + drag moves a whole window.
	if (!select_mode)
	{
		hovered.reset();
		if (over_canvas && io.KeyAlt && ImGui::IsMouseClicked(0))
		{
			Rml::Element* hit = PickAt(mx, my);
			if (hit && IsGameElement(hit))
			{
				drag = Drag::MoveWindow;
				drag_window = hit->GetOwnerDocument();
				drag_x0 = mx;
				drag_y0 = my;
				window_drag_start = drag_window->GetAbsoluteOffset(Rml::BoxArea::Margin);
				viewer->BringToFront(drag_window);
			}
		}
		if (drag == Drag::MoveWindow)
		{
			if (ImGui::IsMouseDown(0) && drag_window)
			{
				drag_window->SetProperty(Rml::PropertyId::Left, Rml::Property(std::round(window_drag_start.x + mx - drag_x0), Rml::Unit::PX));
				drag_window->SetProperty(Rml::PropertyId::Top, Rml::Property(std::round(window_drag_start.y + my - drag_y0), Rml::Unit::PX));
			}
			else
			{
				if (drag_window)
				{
					Rml::Vector2f p = drag_window->GetAbsoluteOffset(Rml::BoxArea::Margin);
					viewer->SetDocumentPosition(viewer->PathOf(drag_window), (int)p.x, (int)p.y);
					viewer->SaveConfig();
				}
				drag = Drag::None;
				drag_window = nullptr;
			}
		}
		return;
	}

	if (drag == Drag::MoveWindow)
	{
		if (ImGui::IsMouseDown(0) && drag_window)
		{
			drag_window->SetProperty(Rml::PropertyId::Left, Rml::Property(std::round(window_drag_start.x + mx - drag_x0), Rml::Unit::PX));
			drag_window->SetProperty(Rml::PropertyId::Top, Rml::Property(std::round(window_drag_start.y + my - drag_y0), Rml::Unit::PX));
		}
		else
		{
			if (drag_window)
			{
				Rml::Vector2f p = drag_window->GetAbsoluteOffset(Rml::BoxArea::Margin);
				viewer->SetDocumentPosition(viewer->PathOf(drag_window), (int)p.x, (int)p.y);
				viewer->SaveConfig();
			}
			drag = Drag::None;
			drag_window = nullptr;
		}
		return;
	}
	if (drag == Drag::None)
	{
		if (!over_canvas)
		{
			hovered.reset();
			return;
		}
		Rml::Element* hit = PickAt(mx, my);
		hovered = hit ? hit->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>();
		const int handle = HandleAt(mx, my);
		if (handle >= 0 && handle < 8)
			ImGui::SetMouseCursor((handle == 1 || handle == 5) ? ImGuiMouseCursor_ResizeNS
					: (handle == 3 || handle == 7)          ? ImGuiMouseCursor_ResizeEW
					: (handle == 0 || handle == 4)          ? ImGuiMouseCursor_ResizeNWSE
															: ImGuiMouseCursor_ResizeNESW);
		else if (handle == 8)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

		if (ImGui::IsMouseClicked(0))
		{
			if (handle < 0 || (handle == 8 && hit && hit != selected.get() && !io.KeyShift))
			{
				// Picking: click selects (the deepest element; Shift keeps the current one for dragging).
				Select(hit);
			}
			Rml::Element* e = selected.get();
			if (!e)
				return;
			const int h = HandleAt(mx, my);
			if (h < 0)
				return;
			// The window itself: moving it just moves it around the preview (its place in game is set by the game).
			if (h == 8 && e == e->GetOwnerDocument())
			{
				drag = Drag::MoveWindow;
				drag_window = e->GetOwnerDocument();
				drag_x0 = mx;
				drag_y0 = my;
				window_drag_start = drag_window->GetAbsoluteOffset(Rml::BoxArea::Margin);
				return;
			}
			// Start a move or resize from the element's current geometry.
			drag = (h == 8) ? Drag::Move : Drag::Resize;
			drag_handle = h;
			drag_x0 = mx;
			drag_y0 = my;
			drag_moved = false;
			const auto& cv = e->GetComputedValues();
			const auto pos = cv.position();
			const bool positioned = pos == Rml::Style::Position::Absolute || pos == Rml::Style::Position::Fixed;
			make_relative = pos == Rml::Style::Position::Static;
			const Rml::Box& box = e->GetBox();
			const Rml::Vector2f rel = e->GetRelativeOffset(Rml::BoxArea::Border);
			auto resolved = [&](const char* name, float fallback) {
				const Rml::Property* p = e->GetProperty(name);
				if (!p || p->unit == Rml::Unit::KEYWORD)
					return fallback;
				if (p->unit == Rml::Unit::PX)
					return p->Get<float>();
				if (p->unit == Rml::Unit::DP)
					return p->Get<float>() * viewer->DpRatio();
				return fallback;
			};
			const Rml::Property* lp = e->GetProperty("left");
			const Rml::Property* rp = e->GetProperty("right");
			const Rml::Property* tp = e->GetProperty("top");
			const Rml::Property* bp = e->GetProperty("bottom");
			const bool left_auto = !lp || lp->unit == Rml::Unit::KEYWORD;
			const bool top_auto = !tp || tp->unit == Rml::Unit::KEYWORD;
			use_right = positioned && left_auto && rp && rp->unit != Rml::Unit::KEYWORD;
			use_bottom = positioned && top_auto && bp && bp->unit != Rml::Unit::KEYWORD;
			const float ml = box.GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Left);
			const float mt = box.GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Top);
			if (use_right)
				base_left = resolved("right", 0);
			else
				base_left = positioned ? resolved("left", rel.x - ml) : resolved("left", 0);
			if (use_bottom)
				base_top = resolved("bottom", 0);
			else
				base_top = positioned ? resolved("top", rel.y - mt) : resolved("top", 0);
			const bool border_box = cv.box_sizing() == Rml::Style::BoxSizing::BorderBox;
			const Rml::Vector2f size = box.GetSize(border_box ? Rml::BoxArea::Border : Rml::BoxArea::Content);
			base_w = size.x;
			base_h = size.y;
			cur_left = base_left;
			cur_top = base_top;
			cur_w = base_w;
			cur_h = base_h;
		}
		return;
	}

	// Dragging
	Rml::Element* e = selected.get();
	if (!e)
	{
		drag = Drag::None;
		return;
	}
	const float dx = mx - drag_x0, dy = my - drag_y0;
	if (!drag_moved && std::fabs(dx) + std::fabs(dy) > 3)
		drag_moved = true;
	if (drag_moved && ImGui::IsMouseDown(0))
	{
		if (drag == Drag::Move)
		{
			cur_left = base_left + (use_right ? -dx : dx);
			cur_top = base_top + (use_bottom ? -dy : dy);
			if (make_relative)
				e->SetProperty("position", "relative");
			e->SetProperty(use_right ? "right" : "left", Fmt(cur_left) + "px");
			e->SetProperty(use_bottom ? "bottom" : "top", Fmt(cur_top) + "px");
		}
		else
		{
			const int h = drag_handle;
			const bool l = h == 0 || h == 6 || h == 7, r = h == 2 || h == 3 || h == 4;
			const bool t = h == 0 || h == 1 || h == 2, b = h == 4 || h == 5 || h == 6;
			cur_w = base_w + (r ? dx : l ? -dx : 0);
			cur_h = base_h + (b ? dy : t ? -dy : 0);
			cur_w = std::max(1.f, cur_w);
			cur_h = std::max(1.f, cur_h);
			if (l || r)
				e->SetProperty("width", Fmt(cur_w) + "px");
			if (t || b)
				e->SetProperty("height", Fmt(cur_h) + "px");
			// Dragging the left/top edge also moves the element (when it's positioned).
			if ((l && !use_right) || (t && !use_bottom))
			{
				if (make_relative)
					e->SetProperty("position", "relative");
				if (l && !use_right)
				{
					cur_left = base_left + (base_w - cur_w);
					e->SetProperty("left", Fmt(cur_left) + "px");
				}
				if (t && !use_bottom)
				{
					cur_top = base_top + (base_h - cur_h);
					e->SetProperty("top", Fmt(cur_top) + "px");
				}
			}
		}
	}
	if (!ImGui::IsMouseDown(0))
	{
		if (drag_moved)
			FinishDrag();
		drag = Drag::None;
	}
}

void Editor::Nudge(int dx, int dy)
{
	Rml::Element* e = selected.get();
	if (!e)
		return;
	if (!nudge_dx && !nudge_dy)
	{
		// Capture the starting point like a move drag.
		const auto& cv = e->GetComputedValues();
		const auto pos = cv.position();
		const bool positioned = pos == Rml::Style::Position::Absolute || pos == Rml::Style::Position::Fixed;
		make_relative = pos == Rml::Style::Position::Static;
		const Rml::Box& box = e->GetBox();
		const Rml::Vector2f rel = e->GetRelativeOffset(Rml::BoxArea::Border);
		auto resolved = [&](const char* name, float fallback) {
			const Rml::Property* p = e->GetProperty(name);
			if (!p || p->unit == Rml::Unit::KEYWORD)
				return fallback;
			if (p->unit == Rml::Unit::PX)
				return p->Get<float>();
			if (p->unit == Rml::Unit::DP)
				return p->Get<float>() * viewer->DpRatio();
			return fallback;
		};
		const Rml::Property* lp = e->GetProperty("left");
		const Rml::Property* rp = e->GetProperty("right");
		const Rml::Property* tp = e->GetProperty("top");
		const Rml::Property* bp = e->GetProperty("bottom");
		use_right = positioned && (!lp || lp->unit == Rml::Unit::KEYWORD) && rp && rp->unit != Rml::Unit::KEYWORD;
		use_bottom = positioned && (!tp || tp->unit == Rml::Unit::KEYWORD) && bp && bp->unit != Rml::Unit::KEYWORD;
		base_left = use_right ? resolved("right", 0) : positioned ? resolved("left", rel.x - box.GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Left)) : resolved("left", 0);
		base_top = use_bottom ? resolved("bottom", 0) : positioned ? resolved("top", rel.y - box.GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Top)) : resolved("top", 0);
	}
	nudge_dx += dx;
	nudge_dy += dy;
	nudge_time = clk::now();
	const float dp = viewer->DpRatio();
	cur_left = base_left + (use_right ? -nudge_dx : nudge_dx) * dp;
	cur_top = base_top + (use_bottom ? -nudge_dy : nudge_dy) * dp;
	if (make_relative)
		e->SetProperty("position", "relative");
	e->SetProperty(use_right ? "right" : "left", Fmt(cur_left) + "px");
	e->SetProperty(use_bottom ? "bottom" : "top", Fmt(cur_top) + "px");
	drag = Drag::None;
}

void Editor::FinishDrag()
{
	const float dp = viewer->DpRatio();
	auto to_dp = [&](float px) { return Fmt(std::round(px / dp)) + "dp"; };
	SourceEdit::Decls decls;
	std::string label;
	if (drag == Drag::Resize)
	{
		const int h = drag_handle;
		const bool l = h == 0 || h == 6 || h == 7, r = h == 2 || h == 3 || h == 4;
		const bool t = h == 0 || h == 1 || h == 2, b = h == 4 || h == 5 || h == 6;
		if (l || r)
			decls.push_back({"width", to_dp(cur_w)});
		if (t || b)
			decls.push_back({"height", to_dp(cur_h)});
		if (l && !use_right)
			decls.push_back({"left", to_dp(cur_left)});
		if (t && !use_bottom)
			decls.push_back({"top", to_dp(cur_top)});
		if (make_relative && ((l && !use_right) || (t && !use_bottom)))
			decls.insert(decls.begin(), {"position", "relative"});
		label = "resize";
	}
	else
	{
		if (make_relative)
			decls.push_back({"position", "relative"});
		decls.push_back({use_right ? "right" : "left", to_dp(cur_left)});
		decls.push_back({use_bottom ? "bottom" : "top", to_dp(cur_top)});
		label = "move";
	}
	nudge_dx = nudge_dy = 0;
	drag = Drag::None;
	if (!CommitDecls(decls, label))
	{
		// Not saved (yet): undo the preview unless we're waiting for an id.
		if (!want_id_popup && selected)
			for (auto& d : decls)
				selected->RemoveProperty(d.first);
	}
}

// ---------------------------------------------------------------- saving

std::vector<std::string> Editor::SelectorChoices(Rml::Element* e) const
{
	std::vector<std::string> v;
	if (!e)
		return v;
	const std::string id = e->GetId();
	if (!id.empty())
		v.push_back("#" + id);
	std::vector<std::string> classes;
	std::string cls = e->GetClassNames();
	size_t i = 0;
	while (i < cls.size())
	{
		size_t j = cls.find(' ', i);
		std::string c = cls.substr(i, j == std::string::npos ? std::string::npos : j - i);
		if (!c.empty() && c != "viewer-has-item" && c.rfind("sp-theme-", 0) != 0 && c.rfind("sp-on-", 0) != 0)
			classes.push_back(c);
		if (j == std::string::npos)
			break;
		i = j + 1;
	}
	for (auto& c : classes)
		v.push_back("." + c);
	if (classes.size() > 1)
	{
		std::string all;
		for (auto& c : classes)
			all += "." + c;
		v.push_back(all);
	}
	return v;
}

std::string Editor::TargetSelector(Rml::Element* e)
{
	if (!target_selector.empty())
		return target_selector;
	auto choices = SelectorChoices(e);
	if (!choices.empty() && choices[0][0] == '#')
		return choices[0];
	return ""; // no id: ask for one (or the user picks a class)
}

bool Editor::ReadForEdit(const fs::path& real, std::string& text)
{
	if (CodeTab* t = FindTab(real))
		if (t->dirty)
		{
			if (!t->applied)
			{
				Rml::Log::Message(Rml::Log::LT_WARNING, "%s has typing that isn't in the preview yet; it's applied now, try the change again.",
					FileName(t->ui).c_str());
				GameFiles::SetOverlay(t->real, t->editor->GetText());
				t->applied = true;
				viewer->RequestReload();
				return false;
			}
			text = t->editor->GetText();
			return true;
		}
	return GameFiles::ReadCurrent(real, text);
}

void Editor::WriteSource(const fs::path& real, const std::string& text, const std::string& label, bool record_undo)
{
	std::string before;
	CodeTab* tab = FindTab(real);
	if (tab && tab->dirty)
		before = tab->editor->GetText();
	else
		GameFiles::ReadCurrent(real, before);
	if (before == text)
		return;
	if (record_undo)
	{
		undo.push_back({real, before, text, label});
		if (undo.size() > 200)
			undo.erase(undo.begin());
		redo.clear();
	}
	if (tab && tab->dirty)
	{
		// The code panel has unsaved typing in this file: put the change there (not on disk).
		TextEditor::Coordinates c = tab->editor->GetCursorPosition();
		tab->editor->SetText(text);
		tab->editor->SetCursorPosition(c);
		GameFiles::SetOverlay(real, text);
		tab->applied = true;
	}
	else
	{
		if (!GameFiles::WriteAll(real, text))
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "Couldn't save %s (is it read-only?)", real.u8string().c_str());
			return;
		}
		GameFiles::ClearOverlay(real);
		if (tab)
		{
			TextEditor::Coordinates c = tab->editor->GetCursorPosition();
			tab->editor->SetText(text);
			tab->editor->SetCursorPosition(c);
			tab->saved = text;
			tab->dirty = false;
			tab->applied = true;
		}
	}
	Rml::Log::Message(Rml::Log::LT_INFO, "Saved %s in %s", label.c_str(), FileName(real.u8string()).c_str());
	viewer->ReloadAll();
}

bool Editor::CommitDecls(const SourceEdit::Decls& decls, const std::string& label)
{
	Rml::Element* e = selected.get();
	if (!e || sel_doc.empty())
		return false;
	const std::string selector = TargetSelector(e);
	if (selector.empty())
	{
		int fi;
		size_t off;
		const bool in_doc = SourceOf(e, fi, off) && GameFiles::Key(GameFiles::AnnotatedFile(fi)) == GameFiles::Key(GameFiles::Resolve(sel_doc));
		if (!in_doc)
		{
			Rml::Log::Message(Rml::Log::LT_WARNING,
				"This element has no id and isn't written in %s (it comes from a template or a script). Pick a class under \"Apply to\" in Properties.",
				FileName(sel_doc).c_str());
			show_properties = true;
			return false;
		}
		pending_decls = decls;
		pending_label = label;
		std::string suggest = e->GetTagName();
		auto choices = SelectorChoices(e);
		if (!choices.empty())
			suggest = choices[0].substr(1);
		std::replace(suggest.begin(), suggest.end(), '.', '-');
		Rml::ElementDocument* doc = e->GetOwnerDocument();
		std::string candidate = suggest;
		for (int n = 2; doc->GetElementById(candidate); n++)
			candidate = suggest + "-" + std::to_string(n);
		snprintf(id_buffer, sizeof(id_buffer), "%s", candidate.c_str());
		want_id_popup = true;
		return false;
	}
	const fs::path real = GameFiles::Resolve(sel_doc);
	std::string text;
	if (!ReadForEdit(real, text))
		return false;
	if (!SourceEdit::UpsertDocumentRule(text, selector, decls))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Couldn't find where to put the style in %s", FileName(sel_doc).c_str());
		return false;
	}
	WriteSource(real, text, label + " (" + selector + ")");
	return true;
}

bool Editor::CommitDeclsAtSource(const std::string& source_path, int line, const SourceEdit::Decls& decls, const std::string& label)
{
	const fs::path real = GameFiles::Resolve(source_path);
	std::string text;
	if (!ReadForEdit(real, text))
		return false;
	if (!SourceEdit::UpsertRuleAtLine(text, line, decls))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Couldn't find the rule at line %d of %s", line, FileName(source_path).c_str());
		return false;
	}
	WriteSource(real, text, label);
	return true;
}

bool Editor::SetElementAttribute(Rml::Element* e, const std::string& name, const std::string& value, bool remove)
{
	int fi;
	size_t off;
	if (!SourceOf(e, fi, off))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "This element was made by a script or data binding, so there's no tag in a file to change.");
		return false;
	}
	const fs::path real = GameFiles::AnnotatedFile(fi);
	std::string text;
	if (!ReadForEdit(real, text))
		return false;
	if (off >= text.size() || text[off] != '<' || !SourceEdit::SetAttribute(text, off, name, value, remove))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "%s changed since it was loaded; reload and try again.", FileName(real.u8string()).c_str());
		return false;
	}
	WriteSource(real, text, (remove ? "remove " : "set ") + name);
	return true;
}

void Editor::Undo()
{
	if (undo.empty())
		return;
	UndoStep s = undo.back();
	undo.pop_back();
	WriteSource(s.real, s.before, "undo " + s.label, false);
	redo.push_back(s);
}

void Editor::Redo()
{
	if (redo.empty())
		return;
	UndoStep s = redo.back();
	redo.pop_back();
	WriteSource(s.real, s.after, "redo " + s.label, false);
	undo.push_back(s);
}

bool Editor::SelectAt(float x, float y)
{
	Rml::Element* e = PickAt(x, y);
	Select(e);
	return e != nullptr;
}

bool Editor::SelectId(const std::string& id)
{
	for (auto& ui : viewer->OpenDocuments())
		if (Rml::ElementDocument* d = viewer->LoadedDocument(ui))
			if (Rml::Element* e = d->GetElementById(id))
			{
				Select(e);
				return true;
			}
	return false;
}

std::string Editor::SelectedBox() const
{
	Rml::Element* e = selected.get();
	if (!e)
		return "";
	Rml::Vector2f p = e->GetAbsoluteOffset(Rml::BoxArea::Border), s = e->GetBox().GetSize(Rml::BoxArea::Border);
	char b[96];
	snprintf(b, sizeof(b), "%.0f,%.0f,%.0f,%.0f", p.x, p.y, s.x, s.y);
	return std::string(b) + " [" + e->GetClassNames() + "]";
}

bool Editor::CommitStyle(const std::string& property, const std::string& value) { return CommitDecls({{property, value}}, property); }

// ---------------------------------------------------------------- overlay

void Editor::DrawOverlay()
{
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImVec2 fbs = ImGui::GetIO().DisplayFramebufferScale;
	auto rect_of = [&](Rml::Element* e, Rml::BoxArea area, ImVec2& a, ImVec2& b) {
		Rml::Vector2f p = e->GetAbsoluteOffset(area);
		Rml::Vector2f s = e->GetBox().GetSize(area);
		a = ImVec2(p.x / fbs.x, p.y / fbs.y);
		b = ImVec2((p.x + s.x) / fbs.x, (p.y + s.y) / fbs.y);
	};
	if (Rml::Element* h = hovered.get())
		if (h != selected.get())
		{
			ImVec2 a, b;
			rect_of(h, Rml::BoxArea::Border, a, b);
			dl->AddRectFilled(a, b, IM_COL32(90, 160, 255, 28));
			dl->AddRect(a, b, IM_COL32(90, 160, 255, 200), 0, 0, 1.f);
			std::string label = Describe(h);
			if (label.size() > 60)
				label = label.substr(0, 57) + "...";
			dl->AddText(ImVec2(a.x, std::max(0.f, a.y - ImGui::GetFontSize() - 2)), IM_COL32(150, 200, 255, 255), label.c_str());
		}
	if (Rml::Element* e = selected.get())
	{
		ImVec2 a, b, pa, pb;
		rect_of(e, Rml::BoxArea::Border, a, b);
		rect_of(e, Rml::BoxArea::Content, pa, pb);
		const ImU32 brass = IM_COL32(220, 170, 80, 255);
		if (pa.x != a.x || pa.y != a.y || pb.x != b.x || pb.y != b.y)
			dl->AddRect(pa, pb, IM_COL32(120, 200, 120, 120), 0, 0, 1.f); // content box
		dl->AddRect(a, b, brass, 0, 0, 2.f);
		if (select_mode)
		{
			const float r = 4.f * ui_scale;
			const float hx[8] = {a.x, (a.x + b.x) / 2, b.x, b.x, b.x, (a.x + b.x) / 2, a.x, a.x};
			const float hy[8] = {a.y, a.y, a.y, (a.y + b.y) / 2, b.y, b.y, b.y, (a.y + b.y) / 2};
			for (int i = 0; i < 8; i++)
			{
				dl->AddRectFilled(ImVec2(hx[i] - r, hy[i] - r), ImVec2(hx[i] + r, hy[i] + r), IM_COL32(30, 25, 20, 255));
				dl->AddRect(ImVec2(hx[i] - r, hy[i] - r), ImVec2(hx[i] + r, hy[i] + r), brass, 0, 0, 1.5f);
			}
		}
		char size[160];
		const float dp = viewer->DpRatio();
		std::string name = Describe(e);
		if (name.size() > 48)
			name = name.substr(0, 45) + "...";
		snprintf(size, sizeof(size), "%s  %d x %d dp", name.c_str(), (int)std::round((b.x - a.x) * fbs.x / dp), (int)std::round((b.y - a.y) * fbs.y / dp));
		dl->AddText(ImVec2(a.x, b.y + 3), brass, size);
	}
}

// ---------------------------------------------------------------- Elements panel

static void TreeContains(Rml::Element* root, Rml::Element* target, bool& out)
{
	for (Rml::Element* c = target; c; c = c->GetParentNode())
		if (c == root)
		{
			out = true;
			return;
		}
	out = false;
}

void Editor::DrawElements()
{
	if (!ImGui::Begin("Elements", &show_elements))
	{
		ImGui::End();
		return;
	}
	const auto& open = viewer->OpenDocuments();
	if (open.empty())
	{
		ImGui::TextWrapped("Open a window from the toolbar to see its elements here.");
		ImGui::End();
		return;
	}
	const std::string win = ActiveWindow();
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo("##win", FileName(win).c_str()))
	{
		for (auto& ui : open)
			if (ImGui::Selectable(FileName(ui).c_str(), ui == win))
				active_window = ui;
		ImGui::EndCombo();
	}
	Rml::ElementDocument* doc = viewer->LoadedDocument(win);
	if (!doc)
	{
		ImGui::TextColored(kErr, "This window didn't load; see the Log.");
		ImGui::End();
		return;
	}
	static Rml::Element* last_selected = nullptr;
	const bool selection_changed = selected.get() != last_selected;
	last_selected = selected.get();

	ImGui::BeginChild("tree", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	Rml::Element* hover_in_tree = nullptr;
	std::function<void(Rml::Element*)> node = [&](Rml::Element* e) {
		const std::string tag = e->GetTagName();
		if (tag == "#text")
			return;
		int kids = 0;
		for (int i = 0; i < e->GetNumChildren(); i++)
			if (e->GetChild(i)->GetTagName() != "#text")
				kids++;
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick;
		if (!kids)
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (e == selected.get())
			flags |= ImGuiTreeNodeFlags_Selected;
		if (selection_changed && selected)
		{
			bool contains = false;
			TreeContains(e, selected.get(), contains);
			if (contains && e != selected.get())
				ImGui::SetNextItemOpen(true);
		}
		if (e == doc)
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		const bool visible = e->IsVisible();
		if (!visible)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		const bool opened = ImGui::TreeNodeEx((void*)e, flags, "%s", Describe(e).c_str());
		if (!visible)
			ImGui::PopStyleColor();
		if (selection_changed && e == selected.get())
			ImGui::SetScrollHereY(0.4f);
		if (ImGui::IsItemHovered())
			hover_in_tree = e;
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			Select(e);
		if (opened && kids)
		{
			for (int i = 0; i < e->GetNumChildren(); i++)
				node(e->GetChild(i));
			ImGui::TreePop();
		}
	};
	node(doc);
	ImGui::EndChild();
	if (hover_in_tree)
		hovered = hover_in_tree->GetObserverPtr();
	ImGui::End();
}

// ---------------------------------------------------------------- Properties panel

void Editor::DrawProperties()
{
	if (!ImGui::Begin("Properties", &show_properties))
	{
		ImGui::End();
		return;
	}
	Rml::Element* e = selected.get();
	if (!e)
	{
		ImGui::TextWrapped("Pick an element: switch to Select (V) and click it in a window, or click it in Elements.");
		ImGui::Spacing();
		ImGui::TextColored(kDim, "Changes are saved into the window's own <style>.");
		ImGui::End();
		return;
	}

	ImGui::TextColored(kBrass, "%s", Describe(e).c_str());
	int fi;
	size_t off;
	const bool has_src = SourceOf(e, fi, off);
	const fs::path doc_real = GameFiles::Resolve(sel_doc);
	if (has_src)
	{
		const fs::path f = GameFiles::AnnotatedFile(fi);
		std::string text;
		int line = 0, col = 0;
		if (ReadForEdit(f, text))
			SourceEdit::OffsetToLineCol(text, off, line, col);
		const bool from_template = GameFiles::Key(f) != GameFiles::Key(doc_real);
		ImGui::TextColored(kDim, "%s, line %d%s", FileName(f.u8string()).c_str(), line + 1, from_template ? "  (template)" : "");
		ImGui::SameLine();
		if (ImGui::SmallButton("Show in code"))
		{
			SourceEdit::Tag tag;
			int len = SourceEdit::FindTag(text, off, tag) ? (int)std::min<size_t>(tag.end - tag.start, 200) : 1;
			GoToSource(GameFiles::ToUiPath(f), line, col, len);
		}
		if (from_template)
			ImGui::TextColored(kWarn, "Comes from a template that other windows use too.");
	}
	else
		ImGui::TextColored(kDim, "Made by a script or data binding (no tag in a file).");

	// id / class (written into the tag)
	if (has_src)
	{
		static std::string id_edit, class_edit;
		static Rml::Element* edit_for = nullptr;
		if (edit_for != e)
		{
			edit_for = e;
			id_edit = e->GetAttribute<Rml::String>("id", "");
			class_edit = e->GetAttribute<Rml::String>("class", "");
		}
		ImGui::SetNextItemWidth(-60 * ui_scale);
		if (ImGui::InputText("id", &id_edit, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			if (id_edit.empty() || ValidId(id_edit))
				SetElementAttribute(e, "id", id_edit, id_edit.empty());
			else
				Rml::Log::Message(Rml::Log::LT_WARNING, "An id can only use letters, numbers, - and _ (and not start with a number).");
			edit_for = nullptr;
		}
		ImGui::SetNextItemWidth(-60 * ui_scale);
		if (ImGui::InputText("class", &class_edit, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			SetElementAttribute(e, "class", class_edit, class_edit.empty());
			edit_for = nullptr;
		}
		ImGui::SetItemTooltip("Press Enter to save");
	}

	// Where changes go
	ImGui::SeparatorText("Apply changes to");
	auto choices = SelectorChoices(e);
	std::string target = TargetSelector(e);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo("##target", target.empty() ? "(needs an id - you'll be asked)" : target.c_str()))
	{
		Rml::ElementDocument* doc = e->GetOwnerDocument();
		for (auto& c : choices)
		{
			Rml::ElementList list;
			doc->QuerySelectorAll(list, c);
			std::string label = c + "   (" + std::to_string(list.size()) + (list.size() == 1 ? " element)" : " elements)");
			if (ImGui::Selectable(label.c_str(), c == target))
				target_selector = c;
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("Which elements the change applies to: this one (#id) or every element with a class.\nThe rule is written in this window's <style>.");

	// Properties
	std::map<std::string, std::pair<std::string, Rml::SharedPtr<const Rml::PropertySource>>> local;
	for (auto it = e->IterateLocalProperties(nullptr); !it.AtEnd(); ++it)
	{
		auto [name, prop] = *it;
		local[name] = {prop.ToString(), prop.source};
	}

	auto commit = [&](const std::string& name, const std::string& value) {
		e->RemoveProperty(name); // drop the live preview; the file decides now
		CommitDecls({{name, value}}, name);
	};

	auto row = [&](const std::string& name) {
		ImGui::PushID(name.c_str());
		const char* longhand = LonghandFor(name);
		const std::string read_name = longhand ? longhand : name;
		const Rml::Property* p = e->GetProperty(read_name);
		const bool is_local = local.count(read_name) > 0;
		const std::string current = p ? p->ToString() : "";
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		if (is_local)
			ImGui::TextUnformatted(name.c_str());
		else
			ImGui::TextColored(kDim, "%s", name.c_str());
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1);

		if (const EnumProp* en = FindEnum(name))
		{
			if (ImGui::BeginCombo("##v", current.c_str()))
			{
				for (const char* o : en->options)
					if (ImGui::Selectable(o, current == o))
						commit(name, o);
				ImGui::Separator();
				if (ImGui::Selectable("(remove from rule)"))
					commit(name, "");
				ImGui::EndCombo();
			}
		}
		else if (IsColorProp(name) && p && p->unit == Rml::Unit::COLOUR)
		{
			Rml::Colourb c = p->Get<Rml::Colourb>();
			float col[4] = {c.red / 255.f, c.green / 255.f, c.blue / 255.f, c.alpha / 255.f};
			if (ImGui::ColorEdit4("##v", col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_Uint8))
			{
				Rml::Colourb nc((Rml::byte)std::round(col[0] * 255), (Rml::byte)std::round(col[1] * 255), (Rml::byte)std::round(col[2] * 255),
					(Rml::byte)std::round(col[3] * 255));
				e->SetProperty(name, ColorText(nc));
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				Rml::Colourb nc((Rml::byte)std::round(col[0] * 255), (Rml::byte)std::round(col[1] * 255), (Rml::byte)std::round(col[2] * 255),
					(Rml::byte)std::round(col[3] * 255));
				commit(name, ColorText(nc));
			}
		}
		else if (p && p->value.GetType() == Rml::Variant::FLOAT && UnitSuffix(p->unit) && name != "font-weight")
		{
			float v = p->Get<float>();
			const char* unit = UnitSuffix(p->unit);
			const std::string fmt = std::string("%.4g") + (unit[0] == '%' ? "%%" : unit);
			const float speed = (p->unit == Rml::Unit::NUMBER) ? 0.01f : 0.5f;
			if (ImGui::DragFloat("##v", &v, speed, 0, 0, fmt.c_str()))
				e->SetProperty(name, Fmt(v) + unit);
			if (ImGui::IsItemDeactivatedAfterEdit())
				commit(name, Fmt(v) + unit);
			if (ImGui::BeginPopupContextItem("type"))
			{
				static std::string typed;
				if (ImGui::IsWindowAppearing())
					typed = current;
				ImGui::TextUnformatted("Type a value (e.g. 12dp, 50%, auto):");
				ImGui::SetNextItemWidth(200 * ui_scale);
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				if (ImGui::InputText("##typed", &typed, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					commit(name, typed);
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::Selectable("Remove from rule"))
					commit(name, "");
				ImGui::EndPopup();
			}
			ImGui::SetItemTooltip("Drag to change. Right-click to type a value.");
		}
		else
		{
			// While the field is being typed in it keeps its own text; otherwise it shows the current value.
			static std::map<ImGuiID, std::string> buffers;
			const ImGuiID id = ImGui::GetID("##v");
			std::string& buf = buffers[id];
			if (ImGui::GetActiveID() != id)
				buf = current;
			if (ImGui::InputText("##v", &buf, ImGuiInputTextFlags_EnterReturnsTrue))
				commit(name, buf);
			ImGui::SetItemTooltip("Enter to save. Clear it and press Enter to remove it from the rule.");
		}

		// Where the current value comes from
		ImGui::TableSetColumnIndex(2);
		auto lit = local.find(read_name);
		if (lit != local.end() && lit->second.second)
		{
			const auto& src = *lit->second.second;
			const std::string label = FileName(src.path) + ":" + std::to_string(src.line_number);
			if (ImGui::SmallButton(label.c_str()))
				GoToSource(src.path, src.line_number - 1, 0, 0);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Set by  %s", src.rule_name.c_str());
				ImGui::TextColored(kDim, "%s line %d", src.path.c_str(), src.line_number);
				ImGui::TextColored(kDim, "Click to see it. Right-click to change it there instead.");
				ImGui::EndTooltip();
			}
			if (ImGui::BeginPopupContextItem("at-source"))
			{
				static std::string typed;
				if (ImGui::IsWindowAppearing())
					typed = current;
				ImGui::Text("Change %s in  %s", name.c_str(), src.rule_name.c_str());
				ImGui::TextColored(kDim, "(%s line %d - affects everything that rule matches)", FileName(src.path).c_str(), src.line_number);
				ImGui::SetNextItemWidth(220 * ui_scale);
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				if (ImGui::InputText("##at", &typed, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					e->RemoveProperty(name);
					CommitDeclsAtSource(src.path, src.line_number, {{name, typed}}, name + " in " + src.rule_name);
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		else if (lit != local.end())
			ImGui::TextColored(kDim, "live");
		ImGui::PopID();
	};

	std::set<std::string> shown;
	ImGui::BeginChild("props");
	for (const auto& g : Groups())
	{
		if (!ImGui::CollapsingHeader(g.title, ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (auto* p : g.props)
				shown.insert(p);
			continue;
		}
		if (ImGui::BeginTable(g.title, 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 118 * ui_scale);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("from", ImGuiTableColumnFlags_WidthFixed, 110 * ui_scale);
			for (auto* p : g.props)
			{
				// Side-by-side longhands only when they differ from the shorthand row's value or are set.
				std::string n = p;
				shown.insert(n);
				const bool side = (n.rfind("margin-", 0) == 0 || n.rfind("padding-", 0) == 0);
				if (side && !local.count(n))
					continue;
				row(n);
			}
			ImGui::EndTable();
		}
	}
	// Anything else set on this element
	std::vector<std::string> others;
	for (auto& [n, v] : local)
		if (!shown.count(n) && n.rfind("border-", 0) != 0)
			others.push_back(n);
	if (!others.empty() && ImGui::CollapsingHeader("Other properties set here", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginTable("others", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 118 * ui_scale);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("from", ImGuiTableColumnFlags_WidthFixed, 110 * ui_scale);
			for (auto& n : others)
				row(n);
			ImGui::EndTable();
		}
	}
	ImGui::SeparatorText("Add a property");
	ImGui::SetNextItemWidth(130 * ui_scale);
	ImGui::InputTextWithHint("##np", "property", &new_prop_name);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-60 * ui_scale);
	const bool enter = ImGui::InputTextWithHint("##nv", "value", &new_prop_value, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	if ((ImGui::Button("Add") || enter) && !new_prop_name.empty() && !new_prop_value.empty())
	{
		if (Rml::StyleSheetSpecification::GetPropertyId(new_prop_name) == Rml::PropertyId::Invalid &&
			Rml::StyleSheetSpecification::GetShorthandId(new_prop_name) == Rml::ShorthandId::Invalid)
			Rml::Log::Message(Rml::Log::LT_WARNING, "'%s' isn't a property RmlUi knows.", new_prop_name.c_str());
		else
		{
			CommitDecls({{new_prop_name, new_prop_value}}, new_prop_name);
			new_prop_name.clear();
			new_prop_value.clear();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

// ---------------------------------------------------------------- Code panel

Editor::CodeTab* Editor::FindTab(const fs::path& real)
{
	const std::string k = GameFiles::Key(real);
	for (auto& t : tabs)
		if (GameFiles::Key(t.real) == k)
			return &t;
	return nullptr;
}

Editor::CodeTab& Editor::OpenTab(const std::string& ui)
{
	const fs::path real = GameFiles::Resolve(ui);
	if (CodeTab* t = FindTab(real))
		return *t;
	CodeTab t;
	t.ui = GameFiles::ToUiPath(real);
	t.real = real;
	t.editor = std::make_unique<TextEditor>();
	t.editor->SetLanguageDefinition(LanguageForFile(ui));
	t.editor->SetPalette(EditorPalette());
	t.editor->SetTabSize(4);
	t.editor->SetShowWhitespaces(false);
	GameFiles::ReadAll(real, t.saved);
	t.editor->SetText(t.saved);
	t.last_disk_check = clk::now();
	tabs.push_back(std::move(t));
	return tabs.back();
}

void Editor::OpenCodeFor(const std::string& ui_path)
{
	OpenTab(ui_path);
	code_active = GameFiles::ToUiPath(GameFiles::Resolve(ui_path));
	goto_tab = code_active;
	show_code = true;
}

void Editor::GoToSource(const std::string& ui, int line, int col, int select_len)
{
	CodeTab& t = OpenTab(ui);
	code_active = t.ui;
	goto_tab = t.ui;
	show_code = true;
	t.editor->SetCursorPosition(TextEditor::Coordinates(line, col));
	if (select_len > 0)
		t.editor->SetSelection(TextEditor::Coordinates(line, col), TextEditor::Coordinates(line, col + select_len));
	ImGui::SetWindowFocus("Code");
}

void Editor::SaveTab(CodeTab& t)
{
	std::string text = t.editor->GetText();
	// TextEditor adds a trailing newline-less text; keep the file's ending
	if (!t.saved.empty() && t.saved.back() == '\n' && (text.empty() || text.back() != '\n'))
		text += '\n';
	if (!GameFiles::WriteAll(t.real, text))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Couldn't save %s", t.real.u8string().c_str());
		return;
	}
	if (text != t.saved)
	{
		undo.push_back({t.real, t.saved, text, "code edit"});
		redo.clear();
	}
	t.saved = text;
	t.dirty = false;
	t.applied = true;
	GameFiles::ClearOverlay(t.real);
	Rml::Log::Message(Rml::Log::LT_INFO, "Saved %s", FileName(t.ui).c_str());
	viewer->ReloadAll();
}

void Editor::SaveAllTabs()
{
	for (auto& t : tabs)
		if (t.dirty)
			SaveTab(t);
}

bool Editor::AnyDirty() const
{
	for (auto& t : tabs)
		if (t.dirty)
			return true;
	return false;
}

static std::string Normalize(const std::string& s)
{
	std::string o;
	o.reserve(s.size());
	for (char c : s)
		if (c != '\r')
			o += c;
	while (!o.empty() && o.back() == '\n')
		o.pop_back();
	return o;
}

void Editor::UpdateTabs()
{
	const auto now = clk::now();
	for (auto& t : tabs)
	{
		// Live preview after a short pause in typing
		if (t.dirty && !t.applied && code_live && now - t.last_edit > std::chrono::milliseconds(500))
		{
			GameFiles::SetOverlay(t.real, t.editor->GetText());
			t.applied = true;
			viewer->RequestReload();
		}
		// Picked up outside changes when there's nothing unsaved here
		if (!t.dirty && now - t.last_disk_check > std::chrono::seconds(1))
		{
			t.last_disk_check = now;
			std::string disk;
			if (GameFiles::ReadAll(t.real, disk) && Normalize(disk) != Normalize(t.saved))
			{
				TextEditor::Coordinates c = t.editor->GetCursorPosition();
				t.saved = disk;
				t.editor->SetText(disk);
				t.editor->SetCursorPosition(c);
			}
		}
	}
}

void Editor::DrawCode()
{
	if (!ImGui::Begin("Code", &show_code))
	{
		ImGui::End();
		return;
	}
	const std::string win = ActiveWindow();
	const WindowFiles* wf = win.empty() ? nullptr : viewer->FilesOf(win);

	// File picker: this window's files
	ImGui::SetNextItemWidth(260 * ui_scale);
	if (ImGui::BeginCombo("##file", "Open a file of this window...", ImGuiComboFlags_HeightLarge))
	{
		if (!wf)
			ImGui::TextDisabled("Open a window first");
		else
		{
			auto list = [&](const char* title, const std::vector<std::string>& files) {
				if (files.empty())
					return;
				ImGui::SeparatorText(title);
				for (auto& f : files)
				{
					std::error_code ec;
					const bool exists = fs::exists(GameFiles::Resolve(f), ec);
					if (ImGui::Selectable(FileName(f).c_str(), false, exists ? 0 : ImGuiSelectableFlags_Disabled))
						OpenCodeFor(f);
					if (!exists)
						ImGui::SetItemTooltip("Not found");
				}
			};
			list("Window", {wf->document});
			list("Stylesheets", wf->stylesheets);
			list("Templates", wf->templates);
			list("Scripts", wf->scripts);
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Live preview", &code_live);
	ImGui::SetItemTooltip("Show changes in the window as you type (before saving)");

	if (tabs.empty())
	{
		if (wf)
			OpenCodeFor(wf->document);
		else
		{
			ImGui::TextWrapped("Open a window to edit its code here.");
			ImGui::End();
			return;
		}
	}

	CodeTab* active = nullptr;
	if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_AutoSelectNewTabs))
	{
		int close_index = -1;
		for (int i = 0; i < (int)tabs.size(); i++)
		{
			CodeTab& t = tabs[i];
			bool open = true;
			ImGuiTabItemFlags flags = t.dirty ? ImGuiTabItemFlags_UnsavedDocument : 0;
			if (goto_tab == t.ui)
				flags |= ImGuiTabItemFlags_SetSelected;
			const std::string label = FileName(t.ui) + "###" + t.ui;
			if (ImGui::BeginTabItem(label.c_str(), &open, flags))
			{
				active = &t;
				code_active = t.ui;
				ImGui::EndTabItem();
			}
			ImGui::SetItemTooltip("%s", t.ui.c_str());
			if (!open)
				close_index = i;
		}
		goto_tab.clear();
		ImGui::EndTabBar();
		if (close_index >= 0)
		{
			CodeTab& t = tabs[close_index];
			if (t.dirty)
				Rml::Log::Message(Rml::Log::LT_WARNING, "%s has unsaved changes. Save them (Ctrl+S) or Revert first.", FileName(t.ui).c_str());
			else
			{
				GameFiles::ClearOverlay(t.real);
				if (active == &t)
					active = nullptr;
				tabs.erase(tabs.begin() + close_index);
			}
		}
	}

	if (active)
	{
		CodeTab& t = *active;
		ImGui::BeginDisabled(!t.dirty);
		if (ImGui::Button("Save"))
			SaveTab(t);
		ImGui::SameLine();
		if (ImGui::Button("Revert"))
		{
			t.editor->SetText(t.saved);
			t.dirty = false;
			t.applied = true;
			GameFiles::ClearOverlay(t.real);
			viewer->RequestReload();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		auto cpos = t.editor->GetCursorPosition();
		ImGui::TextColored(kDim, "Ln %d, Col %d   %s%s", cpos.mLine + 1, cpos.mColumn + 1, t.ui.c_str(), t.dirty ? "  (unsaved)" : "");

		ImGui::PushFont(font_mono, 0.f);
		t.editor->Render("##editor", ImVec2(0, 0), false);
		ImGui::PopFont();
		if (t.editor->IsTextChanged())
		{
			t.dirty = Normalize(t.editor->GetText()) != Normalize(t.saved);
			t.applied = false;
			t.last_edit = clk::now();
			if (!t.dirty)
			{
				GameFiles::ClearOverlay(t.real);
				t.applied = true;
				viewer->RequestReload();
			}
		}
	}
	ImGui::End();
}

// ---------------------------------------------------------------- Sample data panel

static bool LooksLikePercent(const std::string& key)
{
	std::string k = GameFiles::Lower(key);
	return k.find("percent") != std::string::npos || k.find("pct") != std::string::npos;
}

void Editor::DrawSampleData()
{
	if (!ImGui::Begin("Sample data", &show_data))
	{
		ImGui::End();
		return;
	}
	auto& mocks = viewer->Mocks();
	auto names = mocks.Names();
	if (names.empty())
	{
		ImGui::TextWrapped("The values the game would fill in (name, HP, party members...) show up here once a window that uses them is open.");
		ImGui::End();
		return;
	}
	ImGui::TextColored(kDim, "Changes show straight away and are saved in the mock folder.");

	bool changed_any = false;
	std::string changed_model;
	std::function<bool(const std::string&, json&)> draw = [&](const std::string& key, json& j) -> bool {
		bool changed = false;
		ImGui::PushID(key.c_str());
		if (j.is_object())
		{
			if (ImGui::TreeNodeEx(key.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
			{
				for (auto& [k, v] : j.items())
					changed |= draw(k, v);
				ImGui::TreePop();
			}
		}
		else if (j.is_array())
		{
			const std::string label = key + "  [" + std::to_string(j.size()) + "]";
			const bool open = ImGui::TreeNodeEx("arr", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap, "%s", label.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("+"))
			{
				j.push_back(j.empty() ? json("item") : j.back());
				changed = true;
			}
			ImGui::SetItemTooltip("Add an entry (a copy of the last one)");
			ImGui::SameLine();
			ImGui::BeginDisabled(j.empty());
			if (ImGui::SmallButton("-"))
			{
				j.erase(j.size() - 1);
				changed = true;
			}
			ImGui::SetItemTooltip("Remove the last entry");
			ImGui::EndDisabled();
			if (open)
			{
				for (size_t i = 0; i < j.size(); i++)
					changed |= draw(std::to_string(i), j[i]);
				ImGui::TreePop();
			}
		}
		else
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(key.c_str());
			ImGui::SameLine(160 * ui_scale);
			ImGui::SetNextItemWidth(-1);
			if (j.is_boolean())
			{
				bool b = j.get<bool>();
				if (ImGui::Checkbox("##v", &b))
				{
					j = b;
					changed = true;
				}
			}
			else if (j.is_number_integer() || j.is_number_unsigned())
			{
				int v = (int)j.get<int64_t>();
				bool c = LooksLikePercent(key) ? ImGui::SliderInt("##v", &v, 0, 100) : ImGui::DragInt("##v", &v, 1.f);
				if (c)
				{
					j = v;
					changed = true;
				}
			}
			else if (j.is_number_float())
			{
				float v = j.get<float>();
				if (ImGui::DragFloat("##v", &v, 0.1f))
				{
					j = v;
					changed = true;
				}
			}
			else
			{
				std::string s = j.is_string() ? j.get<std::string>() : j.dump();
				if (ImGui::InputText("##v", &s))
				{
					j = s;
					changed = true;
				}
			}
		}
		ImGui::PopID();
		return changed;
	};

	ImGui::BeginChild("data");
	for (auto& name : names)
	{
		json* data = mocks.Data(name);
		if (!data)
			continue;
		ImGui::PushID(name.c_str());
		const bool open = ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
		ImGui::SameLine(ImGui::GetContentRegionMax().x - 70 * ui_scale);
		if (ImGui::SmallButton("Open file") && viewer->Hooks().open_in_explorer)
			viewer->Hooks().open_in_explorer(mocks.FileFor(name));
		if (open)
		{
			bool changed = false;
			for (auto& [k, v] : data->items())
				changed |= draw(k, v);
			if (changed)
			{
				changed_any = true;
				changed_model = name;
			}
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
	if (changed_any)
	{
		mocks.Changed(changed_model);
		mocks.Save(changed_model);
		viewer->NoteOwnWrite();
	}
	ImGui::End();
}

// ---------------------------------------------------------------- Log panel

void Editor::DrawLog()
{
	if (!ImGui::Begin("Log", &show_log))
	{
		ImGui::End();
		return;
	}
	ImGui::Checkbox("Errors", &log_errors);
	ImGui::SameLine();
	ImGui::Checkbox("Warnings", &log_warnings);
	ImGui::SameLine();
	ImGui::Checkbox("Info", &log_info);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(200 * ui_scale);
	ImGui::InputTextWithHint("##filter", "filter", &log_filter);
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear"))
	{
		viewer->System().log.clear();
		viewer->System().errors = viewer->System().warnings = 0;
	}
	ImGui::Separator();
	const auto& log = viewer->System().log;
	std::vector<int> rows;
	const std::string filter = GameFiles::Lower(log_filter);
	for (int i = 0; i < (int)log.size(); i++)
	{
		const auto& e = log[i];
		const bool err = e.type == Rml::Log::LT_ERROR || e.type == Rml::Log::LT_ASSERT;
		const bool warn = e.type == Rml::Log::LT_WARNING;
		if ((err && !log_errors) || (warn && !log_warnings) || (!err && !warn && !log_info))
			continue;
		if (!filter.empty() && GameFiles::Lower(e.text).find(filter) == std::string::npos)
			continue;
		rows.push_back(i);
	}
	ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PushFont(font_mono, 0.f);
	ImGuiListClipper clip;
	clip.Begin((int)rows.size());
	while (clip.Step())
		for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++)
		{
			const auto& e = log[rows[r]];
			const ImVec4 col = (e.type == Rml::Log::LT_ERROR || e.type == Rml::Log::LT_ASSERT) ? kErr : e.type == Rml::Log::LT_WARNING ? kWarn : kDim;
			ImGui::TextColored(kDim, "%s", e.time.c_str());
			ImGui::SameLine();
			ImGui::TextColored(col, "%s", e.text.c_str());
		}
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4)
		ImGui::SetScrollHereY(1.f);
	ImGui::PopFont();
	ImGui::EndChild();
	ImGui::End();
}

// ---------------------------------------------------------------- popups

void Editor::DrawPopups()
{
	if (want_id_popup)
	{
		ImGui::OpenPopup("Give it an id");
		want_id_popup = false;
	}
	if (ImGui::BeginPopupModal("Give it an id", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("This element has no id yet, so its style can't be saved for it alone.");
		ImGui::TextWrapped("Give it one (it's added to its tag), or Cancel and pick a class under \"Apply to\".");
		ImGui::SetNextItemWidth(260 * ui_scale);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		const bool enter = ImGui::InputText("id", id_buffer, sizeof(id_buffer), ImGuiInputTextFlags_EnterReturnsTrue);
		const std::string id = id_buffer;
		const bool valid = ValidId(id) && selected && !selected->GetOwnerDocument()->GetElementById(id);
		if (!id.empty() && !valid)
			ImGui::TextColored(kErr, ValidId(id) ? "Another element already has that id." : "Letters, numbers, - and _ only.");
		ImGui::BeginDisabled(!valid);
		if (ImGui::Button("Save", ImVec2(120 * ui_scale, 0)) || (enter && valid))
		{
			// One edit: the id on the tag and the rule in <style>.
			Rml::Element* e = selected.get();
			int fi;
			size_t off;
			const fs::path real = GameFiles::Resolve(sel_doc);
			std::string text;
			if (e && SourceOf(e, fi, off) && ReadForEdit(real, text) && off < text.size() && text[off] == '<')
			{
				SourceEdit::SetAttribute(text, off, "id", id);
				SourceEdit::UpsertDocumentRule(text, "#" + id, pending_decls);
				target_selector = "#" + id;
				WriteSource(real, text, pending_label + " (#" + id + ")");
			}
			pending_decls.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120 * ui_scale, 0)))
		{
			if (selected)
				for (auto& d : pending_decls)
					selected->RemoveProperty(d.first);
			pending_decls.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (show_help)
	{
		ImGui::OpenPopup("Shortcuts");
		show_help = false;
	}
	if (ImGui::BeginPopupModal("Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		const char* rows[][2] = {
			{"V", "Switch between Interact and Select"},
			{"Click (Select)", "Pick an element"},
			{"Drag (Select)", "Move it; drag the square handles to resize"},
			{"Arrow keys", "Nudge the selected element 1dp (Shift: 10dp)"},
			{"Esc", "Deselect"},
			{"Alt + drag (Interact)", "Move a whole window around the preview"},
			{"Ctrl+S", "Save code"},
			{"Ctrl+Z / Ctrl+Y", "Undo / redo changes to files"},
			{"F5", "Reload everything"},
			{"F8", "RmlUi's own inspector"},
			{"Ctrl + / Ctrl - / Ctrl 0", "Interface size"},
		};
		if (ImGui::BeginTable("keys", 2))
		{
			for (auto& r : rows)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(kBrass, "%s", r[0]);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(r[1]);
			}
			ImGui::EndTable();
		}
		if (ImGui::Button("Close", ImVec2(120 * ui_scale, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (close_popup)
	{
		ImGui::OpenPopup("Unsaved code");
		close_popup = false;
	}
	if (ImGui::BeginPopupModal("Unsaved code", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("These files have changes that aren't saved:");
		for (auto& t : tabs)
			if (t.dirty)
				ImGui::BulletText("%s", FileName(t.ui).c_str());
		if (ImGui::Button("Save and close", ImVec2(140 * ui_scale, 0)))
		{
			SaveAllTabs();
			close_allowed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Close without saving", ImVec2(170 * ui_scale, 0)))
		{
			close_allowed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(100 * ui_scale, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

bool Editor::ConfirmClose()
{
	if (close_allowed || !AnyDirty())
		return true;
	close_popup = true;
	return false;
}
