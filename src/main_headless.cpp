// Headless renderer: loads UI windows and writes a PNG. Used for testing (and handy for quick screenshots).
//   RoseUiShot --game <ROSE folder> --app <settings dir> --out shot.png [--size 1280x720] [--frames 30]
//              [--open Data/ui/themes/rose/game/hud.html]... [--at x,y]...
#include "Viewer.h"
#include "ViewerRenderer.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <cstring>
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
	std::string game, app = ".", out = "shot.png";
	int w = 1280, h = 720, frames = 20, frame_ms = 0;
	std::vector<std::string> open;
	std::vector<std::pair<int, int>> at;
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
		else if (a == "--open")
			open.push_back(next());
		else if (a == "--at")
		{
			int x = 0, y = 0;
			sscanf(next().c_str(), "%d,%d", &x, &y);
			at.push_back({x, y});
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
	fprintf(stderr, "%s\n", msg.c_str());

	{
		ViewerRenderer renderer;
		renderer.SetViewport(w, h);
		Viewer viewer;
		if (!game.empty())
			GameFiles::SetRoot(fs::u8path(game));
		if (!viewer.Init(fs::absolute(fs::u8path(app)), &renderer, nullptr, {}, {w, h}, 1.0f))
		{
			fprintf(stderr, "viewer init failed\n");
			return 1;
		}
		if (!game.empty())
			viewer.SetGameFolder(fs::u8path(game));
		for (size_t i = 0; i < open.size(); i++)
		{
			viewer.OpenDocument(open[i]);
			if (i < at.size())
				viewer.SetDocumentPosition(open[i], at[i].first, at[i].second);
		}

		for (int f = 0; f < frames; f++)
		{
			viewer.Update();
			renderer.Clear();
			renderer.BeginFrame();
			viewer.Render();
			renderer.EndFrame();
			if (frame_ms > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(frame_ms));
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
		viewer.Shutdown();
	}
	RmlGL3::Shutdown();
	return 0;
}
