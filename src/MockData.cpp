#include "MockData.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataVariable.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Variant.h>
#include <cctype>

using nlohmann::json;

// ---------------------------------------------------------------- JSON-backed data variables

namespace {

class JsonScalar;
class JsonArray;
class JsonObject;
Rml::VariableDefinition* DefinitionFor(json& j);

class JsonScalar final : public Rml::VariableDefinition {
public:
	JsonScalar() : VariableDefinition(Rml::DataVariableType::Scalar) {}
	bool Get(void* ptr, Rml::Variant& v) override
	{
		json& j = *static_cast<json*>(ptr);
		switch (j.type())
		{
		case json::value_t::boolean: v = j.get<bool>(); break;
		case json::value_t::number_integer: v = (int64_t)j.get<int64_t>(); break;
		case json::value_t::number_unsigned: v = (int64_t)j.get<uint64_t>(); break;
		case json::value_t::number_float: v = j.get<double>(); break;
		case json::value_t::string: v = Rml::String(j.get<std::string>()); break;
		case json::value_t::null: v = Rml::String(); break;
		default: v = Rml::String(j.dump()); break;
		}
		return true;
	}
	bool Set(void* ptr, const Rml::Variant& v) override
	{
		json& j = *static_cast<json*>(ptr);
		switch (v.GetType())
		{
		case Rml::Variant::BOOL: j = v.Get<bool>(); break;
		case Rml::Variant::INT:
		case Rml::Variant::INT64:
		case Rml::Variant::UINT:
		case Rml::Variant::UINT64:
		case Rml::Variant::BYTE:
		case Rml::Variant::CHAR: j = v.Get<int64_t>(); break;
		case Rml::Variant::FLOAT:
		case Rml::Variant::DOUBLE: j = v.Get<double>(); break;
		default: j = v.Get<Rml::String>(); break;
		}
		return true;
	}
};

class JsonArray final : public Rml::VariableDefinition {
public:
	JsonArray() : VariableDefinition(Rml::DataVariableType::Array) {}
	int Size(void* ptr) override { return (int)static_cast<json*>(ptr)->size(); }
	Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override
	{
		json& j = *static_cast<json*>(ptr);
		const int n = (int)j.size();
		if (address.index >= 0 && address.index < n)
			return Rml::DataVariable(DefinitionFor(j[address.index]), &j[address.index]);
		if (address.name == "size")
			return Rml::MakeLiteralIntVariable(n);
		return Rml::DataVariable();
	}
};

class JsonObject final : public Rml::VariableDefinition {
public:
	JsonObject() : VariableDefinition(Rml::DataVariableType::Struct) {}
	Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override
	{
		json& j = *static_cast<json*>(ptr);
		auto it = j.find(address.name);
		if (it == j.end())
			return Rml::DataVariable();
		return Rml::DataVariable(DefinitionFor(*it), &*it);
	}
	Rml::StringList ReflectMemberNames() override
	{
		Rml::StringList names;
		// 'ptr' isn't passed here, so the names can't be listed; the debugger copes without them.
		return names;
	}
};

JsonScalar g_scalar;
JsonArray g_array;
JsonObject g_object;

Rml::VariableDefinition* DefinitionFor(json& j)
{
	if (j.is_array())
		return &g_array;
	if (j.is_object())
		return &g_object;
	return &g_scalar;
}

// ---------------------------------------------------------------- sample values

std::string Lower(std::string s)
{
	for (auto& c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

bool Has(const std::string& s, std::initializer_list<const char*> words)
{
	for (auto w : words)
		if (s.find(w) != std::string::npos)
			return true;
	return false;
}

bool LooksNumeric(const std::string& name_in)
{
	const std::string n = Lower(name_in);
	if (n == "id" || n == "key" || n == "type" || n == "slot" || n == "index" || n == "x" || n == "y")
		return true;
	return Has(n, {"hp", "mp", "health", "mana", "fuel", "level", "lvl", "count", "percent", "pct", "exp", "max", "min_", "amount", "zuly", "money", "price",
		"cost", "quantity", "qty", "_id", "index", "num", "time", "weight", "grade", "rank", "stamina", "durability", "life",
		"refine", "_key", "points", "point", "score", "kills", "damage", "dps", "ratio", "opacity", "scale", "speed", "def",
		"atk", "attack", "_x", "_y", "width", "height", "size", "total", "remaining"});
}

json NumberFor(const std::string& name_in)
{
	const std::string n = Lower(name_in);
	if (Has(n, {"max", "total"}))
		return 100;
	if (Has(n, {"percent", "pct"}))
		return 75;
	if (Has(n, {"ratio", "opacity", "scale"}))
		return 1;
	if (Has(n, {"level", "lvl"}))
		return 50;
	if (Has(n, {"hp", "mp", "exp", "health", "mana", "stamina", "durability", "life", "fuel"}))
		return 75;
	if (Has(n, {"count", "qty", "quantity"}))
		return 3;
	if (Has(n, {"zuly", "price", "money", "cost"}))
		return 12345;
	if (n == "id" || n == "key" || n == "type" || n == "slot" || n == "index" || Has(n, {"_id", "_key", "index", "grade"}))
		return 1;
	return 42;
}

bool IsSpriteName(const std::string& name)
{
	const std::string n = Lower(name);
	return Has(n, {"sprite", "icon", "image", "img", "texture"}) && !Has(n, {"_id", "index", "count"});
}

json DefaultFor(const std::string& name, VarHint hint)
{
	// Sprite names can't be guessed; leave them empty rather than naming a sprite that doesn't exist.
	if (IsSpriteName(name) && hint != VarHint::Bool && hint != VarHint::IsArray)
		return "";
	switch (hint)
	{
	case VarHint::Bool: return true;
	case VarHint::Number: return NumberFor(name);
	case VarHint::IsArray: return json::array();
	case VarHint::String:
		if (Has(Lower(name), {"width", "height", "percent"}))
			return "50%";
		return name;
	default: break;
	}
	if (LooksNumeric(name))
		return NumberFor(name);
	return name;
}

json FromLiteral(const std::string& lit)
{
	try
	{
		if (lit.find('.') != std::string::npos)
			return std::stod(lit);
		return std::stoll(lit);
	} catch (...)
	{
		return 0;
	}
}

void Insert(json& node, const std::vector<std::string>& path, size_t i, VarHint hint, const std::string& literal = "")
{
	if (i == path.size())
	{
		if (hint == VarHint::IsArray)
		{
			if (!node.is_array())
				node = json::array();
			return;
		}
		if (node.is_null())
		{
			std::string name;
			for (size_t k = path.size(); k-- > 0;)
				if (path[k] != "[]")
				{
					name = path[k];
					break;
				}
			node = literal.empty() ? DefaultFor(name, hint) : FromLiteral(literal);
		}
		else if (!literal.empty() && node.is_boolean())
			node = FromLiteral(literal); // compared with a number somewhere: it's a number
		return;
	}
	const std::string& seg = path[i];
	if (seg == "[]")
	{
		if (!node.is_array())
			node = json::array();
		if (node.empty())
			node.push_back(nullptr);
		Insert(node[0], path, i + 1, hint, literal);
		return;
	}
	if (!node.is_object())
		node = json::object();
	Insert(node[seg], path, i + 1, hint, literal);
}

// Sample arrays get three entries; text in the copies is numbered so the rows look different.
void Number(json& j, int n)
{
	if (j.is_string())
		j = j.get<std::string>() + " " + std::to_string(n);
	else if (j.is_object())
		for (auto& [k, v] : j.items())
			Number(v, n);
	else if (j.is_array())
		for (auto& v : j)
			Number(v, n);
}

void Finish(json& j)
{
	if (j.is_object())
	{
		for (auto& [k, v] : j.items())
			Finish(v);
	}
	else if (j.is_array())
	{
		if (j.empty())
		{
			j.push_back("item");
		}
		for (auto& v : j)
		{
			if (v.is_null())
				v = "item";
			Finish(v);
		}
		if (j.size() == 1)
		{
			json first = j[0];
			for (int n = 2; n <= 3; n++)
			{
				json copy = first;
				Number(copy, n);
				j.push_back(copy);
			}
		}
	}
}

void FixPlaceholder(json& node, const std::vector<std::string>& path, size_t i, const json& value)
{
	if (i == path.size())
	{
		if (node.is_number_integer() && node.get<int64_t>() == 42)
			node = value;
		return;
	}
	if (path[i] == "[]")
	{
		if (node.is_array())
			for (auto& e : node)
				FixPlaceholder(e, path, i + 1, value);
		return;
	}
	if (node.is_object() && node.contains(path[i]))
		FixPlaceholder(node[path[i]], path, i + 1, value);
}

// Adds what 'skel' has and 'user' lacks; never changes a value the user already has.
void MergeMissing(json& user, const json& skel)
{
	if (user.is_object() && skel.is_object())
	{
		for (auto& [k, v] : skel.items())
		{
			if (!user.contains(k))
				user[k] = v;
			else
				MergeMissing(user[k], v);
		}
	}
	else if (user.is_array() && skel.is_array() && !skel.empty())
	{
		for (auto& e : user)
			MergeMissing(e, skel[0]);
	}
}

} // namespace

fs::path MockModels::FileFor(const std::string& model) const
{
	std::string safe;
	for (char c : model)
		safe += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
	return folder_ / (safe + ".json");
}

void MockModels::Build(Rml::Context* context, const std::map<std::string, ModelScan>& models)
{
	for (const auto& [name, scan] : models)
	{
		json skel = json::object();
		// Literal comparisons first, so 'mode == 0' decides mode's sample value before anything else does.
		for (const auto& v : scan.vars)
			if (!v.literal.empty())
				Insert(skel, v.path, 0, v.hint, v.literal);
		for (const auto& v : scan.vars)
			Insert(skel, v.path, 0, v.hint);
		Finish(skel);

		const fs::path file = FileFor(name);
		json user = json::object();
		std::string text;
		bool had_file = GameFiles::ReadAll(file, text);
		if (had_file)
		{
			try
			{
				user = json::parse(text, nullptr, true, true); // allow comments
				if (!user.is_object())
					user = json::object();
			} catch (const std::exception& e)
			{
				Rml::Log::Message(Rml::Log::LT_ERROR, "mock/%s.json has a mistake in it, so it was skipped: %s", name.c_str(), e.what());
				user = skel;
				had_file = true; // don't overwrite the user's broken file
				text.clear();
			}
		}
		const json before = user;
		MergeMissing(user, skel);
		// Older sample files used 42 for every number; where a window compares the value with
		// specific numbers (mode == 0, bag == 1), 42 hides everything, so use the first one instead.
		for (const auto& v : scan.vars)
			if (!v.literal.empty())
				FixPlaceholder(user, v.path, 0, FromLiteral(v.literal));
		if (!had_file || (user != before && !text.empty()))
			GameFiles::WriteAll(file, user.dump(2) + "\n");

		auto holder = std::make_unique<json>(std::move(user));
		Rml::DataModelConstructor ctor = context->CreateDataModel(name, nullptr, true);
		if (!ctor)
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "Couldn't create data model '%s'", name.c_str());
			continue;
		}
		for (auto& [key, value] : holder->items())
			ctor.BindCustomDataVariable(key, Rml::DataVariable(DefinitionFor(value), &value));

		for (const auto& ev : scan.events)
		{
			const std::string model_name = name;
			ctor.BindEventCallback(ev, [model_name, ev](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& args) {
				std::string list;
				for (auto& a : args)
				{
					if (!list.empty())
						list += ", ";
					list += a.Get<Rml::String>();
				}
				Rml::Log::Message(Rml::Log::LT_INFO, "[%s] %s(%s) on %s", model_name.c_str(), ev.c_str(), list.c_str(), event.GetType().c_str());
			});
		}
		for (const auto& tf : scan.transforms)
		{
			if (tf == "to_lower" || tf == "to_upper" || tf == "format" || tf == "round")
				continue;
			ctor.RegisterTransformFunc(tf, [](const Rml::VariantList& args) -> Rml::Variant {
				return args.empty() ? Rml::Variant(Rml::String()) : args[0];
			});
		}
		handles_[name] = ctor.GetModelHandle();
		data_[name] = std::move(holder);
	}
}

std::vector<std::string> MockModels::Names() const
{
	std::vector<std::string> v;
	for (auto& [k, j] : data_)
		v.push_back(k);
	return v;
}

nlohmann::json* MockModels::Data(const std::string& model)
{
	auto it = data_.find(model);
	return it == data_.end() ? nullptr : it->second.get();
}

void MockModels::Changed(const std::string& model)
{
	auto it = handles_.find(model);
	if (it != handles_.end() && it->second)
		it->second.DirtyAllVariables();
}

bool MockModels::Save(const std::string& model)
{
	auto* j = Data(model);
	return j && GameFiles::WriteAll(FileFor(model), j->dump(2) + "\n");
}

void MockModels::RemoveAll(Rml::Context* context)
{
	for (auto& [name, j] : data_)
		context->RemoveDataModel(name);
	data_.clear();
	handles_.clear();
}
