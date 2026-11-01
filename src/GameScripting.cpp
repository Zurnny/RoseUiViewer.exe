#include "GameScripting.h"
#include "GameFiles.h"
#include <RmlUi/Core.h>
#include <RmlUi/Lua/IncludeLua.h>
#include <RmlUi/Lua/Interpreter.h>
#include <RmlUi/Lua/LuaType.h>
#include <cctype>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Rml {
namespace Lua {
RMLUI_LUATYPE_DECLARE(ElementDocument)
}
} // namespace Rml

namespace {

void RunInDocument(Rml::ElementDocument* doc, const std::string& code, const std::string& chunk_name)
{
	lua_State* L = Rml::Lua::Interpreter::GetLuaState();
	if (!L)
		return;
	if (luaL_loadbuffer(L, code.data(), code.size(), chunk_name.c_str()) != LUA_OK)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Lua: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	// _ENV for this chunk: 'document' is this window; everything else reads and writes the shared globals.
	lua_newtable(L);
	Rml::Lua::LuaType<Rml::ElementDocument>::push(L, doc, false);
	lua_setfield(L, -2, "document");
	lua_newtable(L);
	lua_pushglobaltable(L);
	lua_setfield(L, -2, "__index");
	lua_pushglobaltable(L);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	if (!lua_setupvalue(L, -2, 1))
		lua_pop(L, 1);
	if (lua_pcall(L, 0, 0, 0) != LUA_OK)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Lua: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

class GameDocument : public Rml::ElementDocument {
public:
	using Rml::ElementDocument::ElementDocument;

	void RunPending()
	{
		auto list = std::move(pending);
		pending.clear();
		for (auto& [code, name] : list)
			RunInDocument(this, code, name);
	}

protected:
	void LoadInlineScript(const Rml::String& content, const Rml::String& source_path, int source_line) override
	{
		pending.push_back({content, "@" + source_path + ":" + std::to_string(source_line)});
	}
	void LoadExternalScript(const Rml::String& source_path) override
	{
		std::string code;
		if (!GameFiles::ReadCurrent(GameFiles::Resolve(source_path), code))
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "Script not found: %s", source_path.c_str());
			return;
		}
		pending.push_back({code, "@" + source_path});
	}

private:
	std::vector<std::pair<std::string, std::string>> pending;
};

class GameDocumentInstancer : public Rml::ElementInstancer {
public:
	Rml::ElementPtr InstanceElement(Rml::Element* /*parent*/, const Rml::String& tag, const Rml::XMLAttributes& /*attributes*/) override
	{
		return Rml::ElementPtr(new GameDocument(tag));
	}
	void ReleaseElement(Rml::Element* element) override { delete element; }
};

std::function<bool(Rml::Element*, const std::string&, const std::string&)> g_command_handler;

bool IsName(const std::string& s)
{
	if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_'))
		return false;
	for (char c : s)
		if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-'))
			return false;
	return true;
}

// onclick="close", onclick="select 3", onchange="interface-size" ... are commands for the game's C++ code.
class CommandListener : public Rml::EventListener {
public:
	explicit CommandListener(const Rml::String& cmd) : command(cmd) {}

	void ProcessEvent(Rml::Event& event) override
	{
		Rml::Element* el = event.GetCurrentElement();
		Rml::ElementDocument* doc = el ? el->GetOwnerDocument() : nullptr;
		const Rml::String where = doc ? doc->GetSourceURL() : Rml::String();
		const Rml::String first = command.substr(0, command.find_first_of(" :"));
		if (first == "ignore")
			return;
		// name:value sets a data model value (tabs like onclick="mode:1").
		const size_t colon = command.find(':');
		if (colon != Rml::String::npos && command.find(' ') == Rml::String::npos && g_command_handler)
		{
			const std::string name = command.substr(0, colon), value = command.substr(colon + 1);
			if (IsName(name) && !value.empty() && g_command_handler(el, name, value))
			{
				Rml::Log::Message(Rml::Log::LT_INFO, "Game command %s: set %s = %s", command.c_str(), name.c_str(), value.c_str());
				return;
			}
		}
		Rml::Log::Message(Rml::Log::LT_INFO, "Game command (%s): %s  [%s]", event.GetType().c_str(), command.c_str(), where.c_str());
		if (first == "close" && doc)
			doc->Hide();
	}
	void OnDetach(Rml::Element* /*element*/) override { delete this; }

private:
	Rml::String command;
};

class CommandListenerInstancer : public Rml::EventListenerInstancer {
public:
	Rml::EventListener* InstanceEventListener(const Rml::String& value, Rml::Element* /*element*/) override { return new CommandListener(value); }
};

std::unique_ptr<GameDocumentInstancer> g_doc_instancer;
std::unique_ptr<CommandListenerInstancer> g_listener_instancer;

} // namespace

void InstallGameScripting()
{
	g_doc_instancer = std::make_unique<GameDocumentInstancer>();
	g_listener_instancer = std::make_unique<CommandListenerInstancer>();
	Rml::Factory::RegisterElementInstancer("body", g_doc_instancer.get());
	Rml::Factory::RegisterEventListenerInstancer(g_listener_instancer.get());
}

void RunDeferredScripts(Rml::ElementDocument* doc)
{
	if (auto* g = dynamic_cast<GameDocument*>(doc))
		g->RunPending();
}

void SetCommandHandler(std::function<bool(Rml::Element*, const std::string&, const std::string&)> handler)
{
	g_command_handler = std::move(handler);
}

static int g_baseline_ref = LUA_NOREF;

void SnapshotLuaGlobals()
{
	lua_State* L = Rml::Lua::Interpreter::GetLuaState();
	if (!L)
		return;
	lua_newtable(L); // baseline copy
	lua_pushglobaltable(L);
	lua_pushnil(L);
	while (lua_next(L, -2))
	{
		lua_pushvalue(L, -2); // key
		lua_insert(L, -2);    // key, key, value
		lua_settable(L, -5);  // baseline[key] = value
	}
	lua_pop(L, 1); // global table
	g_baseline_ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

void ResetLuaGlobals()
{
	lua_State* L = Rml::Lua::Interpreter::GetLuaState();
	if (!L || g_baseline_ref == LUA_NOREF)
		return;
	const int top = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_baseline_ref); // baseline
	lua_pushglobaltable(L);                            // baseline, G
	// Keys that weren't there at start-up
	std::vector<int> refs;
	lua_newtable(L); // baseline, G, remove-list
	int n = 0;
	lua_pushnil(L);
	while (lua_next(L, -3))
	{
		lua_pop(L, 1); // value
		lua_pushvalue(L, -1);
		lua_rawget(L, -5); // baseline[key]
		const bool known = !lua_isnil(L, -1);
		lua_pop(L, 1);
		if (!known)
		{
			lua_pushvalue(L, -1);
			lua_rawseti(L, -3, ++n);
		}
	}
	for (int i = 1; i <= n; i++)
	{
		lua_rawgeti(L, -1, i); // key
		lua_pushnil(L);
		lua_rawset(L, -4); // G[key] = nil
	}
	lua_pop(L, 1); // remove-list
	// Put back anything a script replaced
	lua_pushnil(L);
	while (lua_next(L, -3))
	{
		lua_pushvalue(L, -2);
		lua_insert(L, -2);
		lua_rawset(L, -4); // G[key] = baseline value
	}
	lua_settop(L, top);
	lua_gc(L, LUA_GCCOLLECT, 0);
}
