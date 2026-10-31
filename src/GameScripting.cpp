#include "GameScripting.h"
#include "GameFiles.h"
#include <RmlUi/Core.h>
#include <RmlUi/Lua/IncludeLua.h>
#include <RmlUi/Lua/Interpreter.h>
#include <RmlUi/Lua/LuaType.h>
#include <memory>

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

protected:
	void LoadInlineScript(const Rml::String& content, const Rml::String& source_path, int source_line) override
	{
		RunInDocument(this, content, "@" + source_path + ":" + std::to_string(source_line));
	}
	void LoadExternalScript(const Rml::String& source_path) override
	{
		std::string code;
		if (!GameFiles::ReadAll(GameFiles::Resolve(source_path), code))
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "Script not found: %s", source_path.c_str());
			return;
		}
		RunInDocument(this, code, "@" + source_path);
	}
};

class GameDocumentInstancer : public Rml::ElementInstancer {
public:
	Rml::ElementPtr InstanceElement(Rml::Element* /*parent*/, const Rml::String& tag, const Rml::XMLAttributes& /*attributes*/) override
	{
		return Rml::ElementPtr(new GameDocument(tag));
	}
	void ReleaseElement(Rml::Element* element) override { delete element; }
};

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
