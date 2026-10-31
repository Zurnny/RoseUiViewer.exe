// ROSE UI Viewer - preview and live-edit ROSE Online's RmlUi windows without starting the game.
#include "RmlUi_Platform_GLFW.h"
#include "Viewer.h"
#include "ViewerRenderer.h"
#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <memory>

#ifdef _WIN32
	#include <windows.h>
	#include <shellapi.h>
	#include <shobjidl.h>
#endif

static GLFWwindow* g_window = nullptr;
static Viewer* g_viewer = nullptr;
static ViewerRenderer* g_renderer = nullptr;
static Rml::Context* g_context = nullptr;
static int g_mods = 0;

static fs::path AppFolder(const char* argv0)
{
#ifdef _WIN32
	wchar_t buf[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	if (n > 0)
		return fs::path(buf).parent_path();
#endif
	std::error_code ec;
	fs::path p = fs::canonical(fs::u8path(argv0), ec);
	return ec ? fs::current_path() : p.parent_path();
}

#ifdef _WIN32
static fs::path PickFolder()
{
	fs::path result;
	IFileOpenDialog* dlg = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
	{
		DWORD opts = 0;
		dlg->GetOptions(&opts);
		dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		dlg->SetTitle(L"Pick your ROSE Online folder (the one with Data in it)");
		if (SUCCEEDED(dlg->Show(nullptr)))
		{
			IShellItem* item = nullptr;
			if (SUCCEEDED(dlg->GetResult(&item)))
			{
				PWSTR path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
				{
					result = fs::path(path);
					CoTaskMemFree(path);
				}
				item->Release();
			}
		}
		dlg->Release();
	}
	return result;
}

static void OpenInExplorer(const fs::path& p) { ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
#else
static fs::path PickFolder() { return {}; }
static void OpenInExplorer(const fs::path& p)
{
	std::string cmd = "xdg-open \"" + p.u8string() + "\" >/dev/null 2>&1 &";
	if (system(cmd.c_str())) {}
}
#endif

static void SetupCallbacks(GLFWwindow* window)
{
	glfwSetKeyCallback(window, [](GLFWwindow*, int key, int, int action, int mods) {
		g_mods = mods;
		if (action == GLFW_PRESS || action == GLFW_REPEAT)
		{
			if (g_viewer->OnKeyDown(RmlGLFW::ConvertKey(key), RmlGLFW::ConvertKeyModifiers(mods)))
				return;
		}
		RmlGLFW::ProcessKeyCallback(g_context, key, action, mods);
	});
	glfwSetCharCallback(window, [](GLFWwindow*, unsigned int cp) { RmlGLFW::ProcessCharCallback(g_context, cp); });
	glfwSetCursorEnterCallback(window, [](GLFWwindow*, int entered) { RmlGLFW::ProcessCursorEnterCallback(g_context, entered); });
	glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
		// Viewer works in framebuffer pixels (same as the context).
		int ww, wh, fw, fh;
		glfwGetWindowSize(w, &ww, &wh);
		glfwGetFramebufferSize(w, &fw, &fh);
		const double sx = ww ? (double)fw / ww : 1.0, sy = wh ? (double)fh / wh : 1.0;
		if (g_viewer->OnMouseMove((int)(x * sx), (int)(y * sy)))
			return;
		RmlGLFW::ProcessCursorPosCallback(g_context, w, x, y, g_mods);
	});
	glfwSetMouseButtonCallback(window, [](GLFWwindow*, int button, int action, int mods) {
		g_mods = mods;
		if (g_viewer->OnMouseButton(button, action == GLFW_PRESS, RmlGLFW::ConvertKeyModifiers(mods)))
			return;
		RmlGLFW::ProcessMouseButtonCallback(g_context, button, action, mods);
	});
	glfwSetScrollCallback(window, [](GLFWwindow*, double, double y) { RmlGLFW::ProcessScrollCallback(g_context, y, g_mods); });
	glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) {
		g_renderer->SetViewport(w, h);
		RmlGLFW::ProcessFramebufferSizeCallback(g_context, w, h);
		g_viewer->OnResize({w, h});
	});
	glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
		std::vector<fs::path> list;
		for (int i = 0; i < count; i++)
			list.push_back(fs::u8path(paths[i]));
		g_viewer->OnDrop(list);
	});
}

int main(int argc, char** argv)
{
#ifdef _WIN32
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
	const fs::path app_dir = AppFolder(argc > 0 ? argv[0] : "");

	if (!glfwInit())
		return 1;
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
	g_window = glfwCreateWindow(1600, 900, "ROSE UI Viewer", nullptr, nullptr);
	if (!g_window)
	{
#ifdef _WIN32
		MessageBoxW(nullptr, L"Couldn't open an OpenGL 3.3 window. Updating your graphics driver usually fixes this.", L"ROSE UI Viewer", MB_ICONERROR);
#endif
		return 1;
	}
	glfwMakeContextCurrent(g_window);
	glfwSwapInterval(1);
	glfwSetInputMode(g_window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);

	Rml::String message;
	if (!RmlGL3::Initialize(&message))
		return 1;

	auto renderer = std::make_unique<ViewerRenderer>();
	auto platform = std::make_unique<SystemInterface_GLFW>(g_window);
	auto viewer = std::make_unique<Viewer>();
	g_renderer = renderer.get();
	g_viewer = viewer.get();

	int fw, fh;
	glfwGetFramebufferSize(g_window, &fw, &fh);
	float content_scale = 1.f;
	glfwGetWindowContentScale(g_window, &content_scale, nullptr);
	renderer->SetViewport(fw, fh);

	PlatformHooks hooks;
	hooks.pick_folder = PickFolder;
	hooks.open_in_explorer = OpenInExplorer;
	hooks.set_title = [](const std::string& t) { glfwSetWindowTitle(g_window, t.c_str()); };

	if (!viewer->Init(app_dir, renderer.get(), platform.get(), hooks, {fw, fh}, content_scale))
		return 1;
	g_context = viewer->GetContext();
	SetupCallbacks(g_window);

	// Command line: a game folder or .html files to open.
	std::vector<fs::path> args;
	for (int i = 1; i < argc; i++)
		args.push_back(fs::u8path(argv[i]));
	if (!args.empty())
		viewer->OnDrop(args);

	while (!glfwWindowShouldClose(g_window))
	{
		glfwWaitEventsTimeout(1.0 / 30.0);
		viewer->Update();
		renderer->Clear();
		renderer->BeginFrame();
		viewer->Render();
		renderer->EndFrame();
		glfwSwapBuffers(g_window);
	}

	viewer->Shutdown();
	viewer.reset();
	platform.reset();
	renderer.reset();
	RmlGL3::Shutdown();
	glfwDestroyWindow(g_window);
	glfwTerminate();
	return 0;
}
