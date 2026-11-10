// ROSE UI Viewer, web version (WebAssembly). Same viewer and editor as the Windows app; files come from a
// folder the user opens in the browser (web/shell.html copies it into memory under /game and saves edits
// back to it). Settings and sample data live in the browser's storage under /app.
#include "Editor.h"
#include "RmlUi_Platform_GLFW.h"
#include "Updater.h"
#include "Viewer.h"
#include "ViewerRenderer.h"
#include <GLFW/emscripten_glfw3.h>
#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <memory>

static GLFWwindow* g_window = nullptr;
static Viewer* g_viewer = nullptr;
static Editor* g_editor = nullptr;
static ViewerRenderer* g_renderer = nullptr;
static Rml::Context* g_context = nullptr;
static Updater* g_updater = nullptr;
static int g_mods = 0;

// ---------------------------------------------------------------- JavaScript side (web/shell.html)

EM_JS(void, js_pick_folder, (), { rose.pickFolder(); });
EM_JS(void, js_reopen_folder, (), { rose.reopen(); });
EM_JS(char*, js_saved_folder_name, (), { return stringToNewUTF8(rose.savedName || ""); });
EM_JS(int, js_can_save_in_place, (), { return rose.canSaveInPlace ? 1 : 0; });
EM_JS(void, js_file_written, (const char* path), { rose.fileWritten(UTF8ToString(path)); });
EM_JS(void, js_download, (const char* path), { rose.download(UTF8ToString(path)); });
EM_JS(void, js_set_title, (const char* title), { document.title = UTF8ToString(title); });
EM_JS(void, js_ready, (), { rose.viewerReady(); });

// Called by the page once a folder has been copied into /game.
extern "C" EMSCRIPTEN_KEEPALIVE void RoseWeb_FolderLoaded()
{
	if (g_viewer)
		g_viewer->RequestGameFolder("/game");
}

// ---------------------------------------------------------------- input (same routing as the Windows app)

static void SetupCallbacks(GLFWwindow* window)
{
	glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int sc, int action, int mods) {
		ImGui_ImplGlfw_KeyCallback(w, key, sc, action, mods);
		g_mods = mods;
		if (g_editor->WantsKeyboard())
			return;
		if ((action == GLFW_PRESS || action == GLFW_REPEAT) && (key == GLFW_KEY_F5 || key == GLFW_KEY_F8))
			return;
		RmlGLFW::ProcessKeyCallback(g_context, key, action, mods);
	});
	glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int cp) {
		ImGui_ImplGlfw_CharCallback(w, cp);
		if (!g_editor->WantsKeyboard())
			RmlGLFW::ProcessCharCallback(g_context, cp);
	});
	glfwSetCursorEnterCallback(window, [](GLFWwindow* w, int entered) {
		ImGui_ImplGlfw_CursorEnterCallback(w, entered);
		RmlGLFW::ProcessCursorEnterCallback(g_context, entered);
	});
	glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
		ImGui_ImplGlfw_CursorPosCallback(w, x, y);
		if (!g_editor->BlocksCanvasMouse())
			RmlGLFW::ProcessCursorPosCallback(g_context, w, x, y, g_mods);
	});
	glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int mods) {
		ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
		g_mods = mods;
		if (!g_editor->BlocksCanvasMouse() || action == GLFW_RELEASE)
			RmlGLFW::ProcessMouseButtonCallback(g_context, button, action, mods);
	});
	glfwSetScrollCallback(window, [](GLFWwindow* w, double xo, double yo) {
		ImGui_ImplGlfw_ScrollCallback(w, xo, yo);
		if (!g_editor->BlocksCanvasMouse())
			RmlGLFW::ProcessScrollCallback(g_context, yo, g_mods);
	});
	glfwSetWindowFocusCallback(window, [](GLFWwindow* w, int focused) { ImGui_ImplGlfw_WindowFocusCallback(w, focused); });
	glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) {
		g_renderer->SetViewport(w, h);
		RmlGLFW::ProcessFramebufferSizeCallback(g_context, w, h);
	});
}

static void Frame()
{
	try
	{
		g_viewer->Update();
	} catch (const std::exception& e)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Something went wrong: %s", e.what());
	}
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	try
	{
		g_editor->Frame();
	} catch (const std::exception& e)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Something went wrong: %s", e.what());
	}
	ImGui::Render();

	int fw, fh;
	glfwGetFramebufferSize(g_window, &fw, &fh);
	g_renderer->SetViewport(fw, fh);
	g_renderer->Clear();
	g_renderer->BeginFrame();
	g_viewer->Render();
	g_renderer->EndFrame();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

int main()
{
	const fs::path app_dir = "/app"; // browser storage (mounted by the page before main runs)

	if (!glfwInit())
		return 1;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE); // sharp on high-DPI screens
	g_window = glfwCreateWindow(1280, 720, "ROSE UI Viewer", nullptr, nullptr);
	if (!g_window)
		return 1;
	glfwMakeContextCurrent(g_window);
	// The canvas fills the browser window.
	emscripten_glfw_make_canvas_resizable(g_window, "window", nullptr);

	Rml::String message;
	if (!RmlGL3::Initialize(&message))
		return 1;

	static auto renderer = std::make_unique<ViewerRenderer>();
	static auto platform = std::make_unique<SystemInterface_GLFW>(g_window);
	static auto viewer = std::make_unique<Viewer>();
	static auto editor = std::make_unique<Editor>();
	static auto updater = std::make_unique<Updater>();
	g_renderer = renderer.get();
	g_viewer = viewer.get();
	g_editor = editor.get();
	g_updater = updater.get();

	int fw, fh;
	glfwGetFramebufferSize(g_window, &fw, &fh);
	float content_scale = 1.f;
	glfwGetWindowContentScale(g_window, &content_scale, nullptr);
	renderer->SetViewport(fw, fh);

	PlatformHooks hooks;
	hooks.is_web = true;
	hooks.pick_folder = [] {
		js_pick_folder(); // asynchronous: the page calls RoseWeb_FolderLoaded when the folder is ready
		return fs::path();
	};
	hooks.open_in_explorer = [](const fs::path& p) {
		std::error_code ec;
		if (fs::is_regular_file(p, ec))
			js_download(p.u8string().c_str());
		else
			Rml::Log::Message(Rml::Log::LT_INFO, "In the web version, sample data is kept in this browser. Each panel's Open file button downloads its file.");
	};
	hooks.set_title = [](const std::string& t) { js_set_title(t.c_str()); };
	hooks.saved_folder_name = [] {
		char* s = js_saved_folder_name();
		std::string r = s ? s : "";
		free(s);
		return r;
	};
	hooks.reopen_folder = [] { js_reopen_folder(); };
	hooks.can_save_in_place = js_can_save_in_place() != 0;

	// Every saved file goes back to the user's folder (or downloads), and settings to browser storage.
	GameFiles::SetWriteHook([](const fs::path& p) { js_file_written(p.generic_u8string().c_str()); });

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplGlfw_InitForOpenGL(g_window, false);
	ImGui_ImplGlfw_InstallEmscriptenCallbacks(g_window, "#canvas");
	ImGui_ImplOpenGL3_Init("#version 300 es");

	if (!viewer->Init(app_dir, renderer.get(), platform.get(), hooks, {fw, fh}, content_scale))
		return 1;
	g_context = viewer->GetContext();
	editor->Init(viewer.get(), content_scale);
	updater->Start(app_dir / "RoseUiViewer", ROSEVIEW_VERSION, false);
	editor->SetUpdater(updater.get());
	SetupCallbacks(g_window);

	js_ready();
	emscripten_set_main_loop(Frame, 0, false);
	return 0;
}
