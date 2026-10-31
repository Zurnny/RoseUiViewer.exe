// Headless renderer: loads UI windows, optionally runs editor actions, and writes a PNG.
// Used for testing (and handy for quick screenshots).
//   RoseUiShot --game <ROSE folder> --app <settings dir> --out shot.png [--size 1600x900] [--frames 30] [--editor]
//              [--open Data/ui/themes/rose/game/hud.html]... [--at x,y]...
//              [--select x,y] [--style name=value]... [--code <ui path>] [--mouse x,y]
#include "Editor.h"
#include <RmlUi/Core/Context.h>
#include "Viewer.h"
#include "ViewerRenderer.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <stb_image_write.h>
#include <string>
#include <thread>
#include <vector>

typedef void (*PFN_glReadPixels)(int, int, int, int, unsigned, unsigned, void*);
typedef void (*PFN_glFinish)();
#define GL_RGBA_ 0x1908
#define GL_UNSIGNED_BYTE_ 0x1401

int main(int argc, char** argv)
{
	std::string game, app = ".", out = "shot.png", code, target, select_id, panel;
	float drag[4] = {-1, -1, -1, -1};
	int w = 1600, h = 900, frames = 20, frame_ms = 0;
	bool use_editor = false, center = false;
	float mouse_x = -1, mouse_y = -1;
	std::vector<std::string> open;
	std::vector<std::pair<int, int>> at;
	std::vector<std::pair<float, float>> selects, clicks;
	std::vector<std::pair<std::string, std::string>> styles;
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--game")
			game = next();
		else if (a == "--app")
			app = next();
		else if (a == "--out")
			out = next();
		else if (a == "--size")
			sscanf(next().c_str(), "%dx%d", &w, &h);
		else if (a == "--frame-ms")
			frame_ms = std::stoi(next());
		else if (a == "--frames")
			frames = std::stoi(next());
		else if (a == "--editor")
			use_editor = true;
		else if (a == "--open")
			open.push_back(next());
		else if (a == "--code")
			code = next();
		else if (a == "--at")
		{
			int x = 0, y = 0;
			sscanf(next().c_str(), "%d,%d", &x, &y);
			at.push_back({x, y});
		}
		else if (a == "--select")
		{
			float x = 0, y = 0;
			sscanf(next().c_str(), "%f,%f", &x, &y);
			selects.push_back({x, y});
			use_editor = true;
		}
		else if (a == "--select-id")
		{
			select_id = next();
			use_editor = true;
		}
		else if (a == "--drag")
		{
			sscanf(next().c_str(), "%f,%f,%f,%f", &drag[0], &drag[1], &drag[2], &drag[3]);
			use_editor = true;
		}
		else if (a == "--click")
		{
			float x = 0, y = 0;
			sscanf(next().c_str(), "%f,%f", &x, &y);
			clicks.push_back({x, y});
		}
		else if (a == "--center")
			center = true;
		else if (a == "--panel")
			panel = next();
		else if (a == "--target")
			target = next();
		else if (a == "--mouse")
			sscanf(next().c_str(), "%f,%f", &mouse_x, &mouse_y);
		else if (a == "--style")
		{
			std::string kv = next();
			size_t eq = kv.find('=');
			if (eq != std::string::npos)
				styles.push_back({kv.substr(0, eq), kv.substr(eq + 1)});
			use_editor = true;
		}
	}

	out = fs::absolute(fs::u8path(out)).u8string();
	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0, minor = 0;
	if (!eglInitialize(dpy, &major, &minor))
	{
		fprintf(stderr, "eglInitialize failed\n");
		return 1;
	}
	const EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_STENCIL_SIZE,
		8, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
	EGLConfig cfg;
	EGLint n = 0;
	if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
	{
		fprintf(stderr, "no EGL config\n");
		return 1;
	}
	const EGLint pb_attr[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE};
	EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb_attr);
	eglBindAPI(EGL_OPENGL_API);
	const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
		EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (!ctx || !eglMakeCurrent(dpy, surf, surf, ctx))
	{
		fprintf(stderr, "EGL context failed\n");
		return 1;
	}

	Rml::String msg;
	if (!RmlGL3::Initialize(&msg))
	{
		fprintf(stderr, "GL init failed: %s\n", msg.c_str());
		return 1;
	}

	{
		ViewerRenderer renderer;
		renderer.SetViewport(w, h);
		Viewer viewer;
		Editor editor;
		if (!game.empty())
			GameFiles::SetRoot(fs::u8path(game));
		if (use_editor)
		{
			ImGui::CreateContext();
			ImGui::GetIO().DisplaySize = ImVec2((float)w, (float)h);
			ImGui_ImplOpenGL3_Init("#version 330");
		}
		if (!viewer.Init(fs::absolute(fs::u8path(app)), &renderer, nullptr, {}, {w, h}, 1.0f))
		{
			fprintf(stderr, "viewer init failed\n");
			return 1;
		}
		if (use_editor)
			editor.Init(&viewer, 1.0f);
		if (!game.empty())
			viewer.SetGameFolder(fs::u8path(game));
		for (size_t i = 0; i < open.size(); i++)
		{
			viewer.OpenDocument(open[i]);
			if (i < at.size())
				viewer.SetDocumentPosition(open[i], at[i].first, at[i].second);
		}

		auto frame = [&](int f) {
			viewer.Update();
			if (use_editor)
			{
				ImGuiIO& io = ImGui::GetIO();
				io.DeltaTime = 1.f / 60.f;
				if (mouse_x >= 0)
					io.AddMousePosEvent(mouse_x, mouse_y);
				ImGui_ImplOpenGL3_NewFrame();
				ImGui::NewFrame();
				editor.Frame();
				ImGui::Render();
			}
			renderer.Clear();
			renderer.BeginFrame();
			viewer.Render();
			renderer.EndFrame();
			if (use_editor)
				ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			if (frame_ms > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(frame_ms));
			(void)f;
		};

		for (int f = 0; f < frames; f++)
		{
			frame(f);
			// Scripted editor actions, after the layout has settled.
			if (f == 3)
			{
				for (auto& s : selects)
					fprintf(stderr, "select %.0f,%.0f -> %s\n", s.first, s.second, editor.SelectAt(s.first, s.second) ? "ok" : "nothing");
				if (!code.empty())
					editor.OpenCodeFor(code);
				if (!target.empty())
					editor.SetTarget(target);
				if (!panel.empty())
					editor.FocusPanel(panel);
				if (center)
					viewer.CenterAll();
				if (!select_id.empty())
				{
					const bool ok = editor.SelectId(select_id);
					fprintf(stderr, "select #%s -> %s  box %s\n", select_id.c_str(), ok ? "ok" : "nothing", editor.SelectedBox().c_str());
				}
			}
			// Simulated mouse drag in select mode: frames 6..14
			if (drag[0] >= 0)
			{
				ImGuiIO& io = ImGui::GetIO();
				if (f == 5)
				{
					editor.SetSelectMode(true);
					io.AddMousePosEvent(drag[0], drag[1]);
				}
				else if (f == 6)
					io.AddMouseButtonEvent(0, true);
				else if (f >= 7 && f <= 12)
				{
					float t = (f - 6) / 6.f;
					io.AddMousePosEvent(drag[0] + (drag[2] - drag[0]) * t, drag[1] + (drag[3] - drag[1]) * t);
				}
				else if (f == 13)
					io.AddMouseButtonEvent(0, false);
				else if (f == 16)
					fprintf(stderr, "after drag: box %s\n", editor.SelectedBox().c_str());
			}
			// Clicks straight into the game windows (Interact mode), one every two frames from frame 6
			for (size_t c = 0; c < clicks.size(); c++)
				if (f == 6 + (int)c * 2)
				{
					Rml::Context* ctx = viewer.GetContext();
					ctx->ProcessMouseMove((int)clicks[c].first, (int)clicks[c].second, 0);
					ctx->ProcessMouseButtonDown(0, 0);
					ctx->ProcessMouseButtonUp(0, 0);
				}
			if (f == 5)
				for (auto& [k, v] : styles)
					fprintf(stderr, "style %s=%s -> %s\n", k.c_str(), v.c_str(), editor.CommitStyle(k, v) ? "saved" : "not saved");
		}
		auto readPixels = (PFN_glReadPixels)eglGetProcAddress("glReadPixels");
		auto finish = (PFN_glFinish)eglGetProcAddress("glFinish");
		finish();
		std::vector<unsigned char> px((size_t)w * h * 4), flipped(px.size());
		readPixels(0, 0, w, h, GL_RGBA_, GL_UNSIGNED_BYTE_, px.data());
		for (int y = 0; y < h; y++)
			memcpy(&flipped[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
		for (size_t i = 3; i < flipped.size(); i += 4)
			flipped[i] = 255;
		stbi_write_png(out.c_str(), w, h, 4, flipped.data(), w * 4);
		fprintf(stderr, "wrote %s\n", out.c_str());
		if (use_editor)
			editor.Shutdown();
		viewer.Shutdown();
		if (use_editor)
		{
			ImGui_ImplOpenGL3_Shutdown();
			ImGui::DestroyContext();
		}
	}
	RmlGL3::Shutdown();
	return 0;
}
