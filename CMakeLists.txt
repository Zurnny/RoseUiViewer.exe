cmake_minimum_required(VERSION 3.20)
project(RoseUiViewer LANGUAGES C CXX)

if(POLICY CMP0169)
	cmake_policy(SET CMP0169 OLD) # allow FetchContent_Populate for header-only/source-only deps
endif()
# One C runtime for everything (static), so the .exe runs on its own.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ROSEVIEW_APP "Build the windowed viewer (GLFW)" ON)
option(ROSEVIEW_HEADLESS "Build the headless screenshot tool (EGL, Linux only)" OFF)
set(ROSEVIEW_EGL_INCLUDE "" CACHE PATH "Folder holding EGL/ and KHR/ headers (headless build)")
set(ROSEVIEW_VERSION "dev" CACHE STRING "Version stamped into the build (the GitHub builds set 1.<build number>)")

# Web version (Emscripten): C++ exceptions and Lua's error handling use WebAssembly's own mechanism.
if(EMSCRIPTEN)
	set(ROSEVIEW_APP OFF CACHE BOOL "" FORCE)
	set(ROSEVIEW_HEADLESS OFF CACHE BOOL "" FORCE)
	add_compile_options(-fwasm-exceptions -sSUPPORT_LONGJMP=wasm)
	add_link_options(-fwasm-exceptions -sSUPPORT_LONGJMP=wasm)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- Lua 5.4
FetchContent_Declare(lua GIT_REPOSITORY https://github.com/lua/lua.git GIT_TAG v5.4.7 GIT_SHALLOW TRUE)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
	FetchContent_Populate(lua)
endif()
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/l*.c)
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/lua.c ${lua_SOURCE_DIR}/luac.c ${lua_SOURCE_DIR}/ltests.c)
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
if(UNIX)
	target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
	target_link_libraries(lua_static PUBLIC m)
endif()
add_library(Lua::Lua INTERFACE IMPORTED GLOBAL)
set_target_properties(Lua::Lua PROPERTIES INTERFACE_LINK_LIBRARIES lua_static)

# ---------------------------------------------------------------- FreeType
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype GIT_REPOSITORY https://github.com/freetype/freetype.git GIT_TAG VER-2-14-3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(freetype)
if(NOT TARGET Freetype::Freetype)
	add_library(Freetype::Freetype ALIAS freetype)
endif()

# ---------------------------------------------------------------- RmlUi
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS ON CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS_LIBRARY "lua" CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(rmlui GIT_REPOSITORY https://github.com/mikke89/RmlUi.git GIT_TAG 6.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(rmlui)

# ---------------------------------------------------------------- json + stb (headers)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
	FetchContent_Populate(json)
endif()
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
	FetchContent_Populate(stb)
endif()

FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG docking GIT_SHALLOW TRUE)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
	FetchContent_Populate(imgui)
endif()

set(RMLUI_BACKENDS ${rmlui_SOURCE_DIR}/Backends)

# The viewer's own toolbar font (Lato, SIL Open Font License), so it shows text before a game folder is chosen.
include(cmake/Embed.cmake)
set(EMBED_DIR ${CMAKE_BINARY_DIR}/embedded)
if(NOT EXISTS ${EMBED_DIR}/font_regular.cpp)
	embed_file(${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf ${EMBED_DIR}/font_regular.cpp g_font_regular)
	embed_file(${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Bold.ttf ${EMBED_DIR}/font_bold.cpp g_font_bold)
endif()
if(NOT EXISTS ${EMBED_DIR}/font_mono.cpp)
	embed_file(${rmlui_SOURCE_DIR}/Samples/assets/RobotoMono-Regular.ttf ${EMBED_DIR}/font_mono.cpp g_font_mono)
endif()

# Dear ImGui (editor panels) + the code editor widget
add_library(imgui_core STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
	${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
	third_party/TextEditor/TextEditor.cpp
)
target_include_directories(imgui_core PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends third_party/TextEditor)
if(EMSCRIPTEN)
	target_compile_definitions(imgui_core PUBLIC IMGUI_IMPL_OPENGL_ES3) # WebGL 2
endif()

# ---------------------------------------------------------------- viewer core
add_library(viewer_core STATIC
	src/Viewer.cpp
	src/GameFiles.cpp
	src/ImageDecode.cpp
	src/MockData.cpp
	src/MarkupScan.cpp
	src/CustomElements.cpp
	src/GameScripting.cpp
	src/ViewerRenderer.cpp
	${RMLUI_BACKENDS}/RmlUi_Renderer_GL3.cpp
	${EMBED_DIR}/font_regular.cpp
	${EMBED_DIR}/font_bold.cpp
	${EMBED_DIR}/font_mono.cpp
	src/SourceEdit.cpp
	src/Editor.cpp
	src/EditorLanguages.cpp
	src/Updater.cpp
)
target_include_directories(viewer_core PUBLIC src ${RMLUI_BACKENDS} ${json_SOURCE_DIR}/single_include ${stb_SOURCE_DIR})
target_link_libraries(viewer_core PUBLIC RmlUi::RmlUi RmlUi::Debugger RmlUi::Lua Lua::Lua imgui_core)
target_compile_definitions(viewer_core PUBLIC ROSEVIEW_VERSION="${ROSEVIEW_VERSION}")
if(WIN32)
	target_link_libraries(viewer_core PUBLIC opengl32 winhttp)
	target_compile_definitions(viewer_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
	target_link_libraries(viewer_core PUBLIC dl)
endif()

# ---------------------------------------------------------------- windowed app
if(ROSEVIEW_APP)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.5.1 GIT_SHALLOW TRUE)
	FetchContent_MakeAvailable(glfw)

	add_executable(RoseUiViewer WIN32 src/main_glfw.cpp ${RMLUI_BACKENDS}/RmlUi_Platform_GLFW.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
	target_compile_definitions(RoseUiViewer PRIVATE GLFW_INCLUDE_NONE)
	target_link_libraries(RoseUiViewer PRIVATE viewer_core glfw)
	if(WIN32)
		target_link_libraries(RoseUiViewer PRIVATE ole32 shell32 uuid comdlg32)
		if(MSVC)
			target_link_options(RoseUiViewer PRIVATE /ENTRY:mainCRTStartup) # windowed app, but starts at main()
		else()
			target_link_options(RoseUiViewer PRIVATE -static -static-libgcc -static-libstdc++)
		endif()
	endif()
endif()

# ---------------------------------------------------------------- headless (testing / screenshots)
if(ROSEVIEW_HEADLESS)
	add_executable(RoseUiShot src/main_headless.cpp)
	target_include_directories(RoseUiShot PRIVATE ${ROSEVIEW_EGL_INCLUDE})
	find_library(EGL_LIB NAMES EGL libEGL.so.1 PATHS /usr/lib/x86_64-linux-gnu)
	target_link_libraries(RoseUiShot PRIVATE viewer_core ${EGL_LIB})
endif()

# ---------------------------------------------------------------- web version (index.html + .js + .wasm)
if(EMSCRIPTEN)
	add_executable(RoseUiViewerWeb src/main_web.cpp ${RMLUI_BACKENDS}/RmlUi_Platform_GLFW.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
	set_target_properties(RoseUiViewerWeb PROPERTIES OUTPUT_NAME index SUFFIX .html)
	target_compile_definitions(RoseUiViewerWeb PRIVATE GLFW_INCLUDE_NONE)
	target_compile_options(RoseUiViewerWeb PRIVATE --use-port=contrib.glfw3)
	target_link_libraries(RoseUiViewerWeb PRIVATE viewer_core)
	target_link_options(RoseUiViewerWeb PRIVATE
		--use-port=contrib.glfw3
		-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
		-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=4194304
		-sFORCE_FILESYSTEM=1 -lidbfs.js
		"-sEXPORTED_FUNCTIONS=['_main','_RoseWeb_FolderLoaded','_malloc','_free']"
		"-sEXPORTED_RUNTIME_METHODS=['ccall','FS','IDBFS','addRunDependency','removeRunDependency','stringToNewUTF8','UTF8ToString']"
		--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html)
	set_property(TARGET RoseUiViewerWeb APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/shell.html)
endif()
