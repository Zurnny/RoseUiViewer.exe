#include "MarkupScan.h"
#include <cctype>
#include <cstring>
#include <algorithm>

namespace {

std::string Lower(std::string s)
{
	for (auto& c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

std::string DecodeEntities(const std::string& s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] == '&')
		{
			static const char* names[][2] = {{"&lt;", "<"}, {"&gt;", ">"}, {"&amp;", "&"}, {"&quot;", "\""}, {"&apos;", "'"}};
			bool done = false;
			for (auto& n : names)
				if (s.compare(i, strlen(n[0]), n[0]) == 0)
				{
					out += n[1];
					i += strlen(n[0]) - 1;
					done = true;
					break;
				}
			if (done)
				continue;
		}
		out += s[i];
	}
	return out;
}

// ---------------------------------------------------------------- expression tokens

enum class T { Ident, Number, String, Op, LParen, RParen, LBracket, RBracket, Dot, Comma, Pipe, Other };
struct Tok {
	T t;
	std::string s;
};

std::vector<Tok> Tokenize(const std::string& e)
{
	std::vector<Tok> v;
	size_t i = 0;
	while (i < e.size())
	{
		char c = e[i];
		if (std::isspace((unsigned char)c))
		{
			i++;
			continue;
		}
		if (std::isalpha((unsigned char)c) || c == '_')
		{
			size_t j = i;
			while (j < e.size() && (std::isalnum((unsigned char)e[j]) || e[j] == '_'))
				j++;
			v.push_back({T::Ident, e.substr(i, j - i)});
			i = j;
			continue;
		}
		if (std::isdigit((unsigned char)c))
		{
			size_t j = i;
			while (j < e.size() && (std::isalnum((unsigned char)e[j]) || e[j] == '.'))
				j++;
			v.push_back({T::Number, e.substr(i, j - i)});
			i = j;
			continue;
		}
		if (c == '\'' || c == '"')
		{
			size_t j = i + 1;
			while (j < e.size() && e[j] != c)
				j++;
			v.push_back({T::String, e.substr(i + 1, j - i - 1)});
			i = j + 1;
			continue;
		}
		switch (c)
		{
		case '(': v.push_back({T::LParen, "("}); i++; continue;
		case ')': v.push_back({T::RParen, ")"}); i++; continue;
		case '[': v.push_back({T::LBracket, "["}); i++; continue;
		case ']': v.push_back({T::RBracket, "]"}); i++; continue;
		case '.': v.push_back({T::Dot, "."}); i++; continue;
		case ',': v.push_back({T::Comma, ","}); i++; continue;
		default: break;
		}
		if (c == '|' && !(i + 1 < e.size() && e[i + 1] == '|'))
		{
			v.push_back({T::Pipe, "|"});
			i++;
			continue;
		}
		// operators: two-char first
		static const char* ops2[] = {"==", "!=", "<=", ">=", "&&", "||"};
		bool matched = false;
		for (auto op : ops2)
			if (e.compare(i, 2, op) == 0)
			{
				v.push_back({T::Op, op});
				i += 2;
				matched = true;
				break;
			}
		if (matched)
			continue;
		v.push_back({T::Op, std::string(1, c)});
		i++;
	}
	return v;
}

bool IsComparison(const std::string& s) { return s == "==" || s == "!=" || s == "<" || s == ">" || s == "<=" || s == ">="; }
bool IsArithmetic(const std::string& s) { return s == "+" || s == "-" || s == "*" || s == "/" || s == "%"; }

struct Scope {
	std::string tag;
	std::string model;
	std::map<std::string, std::vector<std::string>> aliases; // alias -> path; empty path = index (number)
};

class Scanner {
public:
	DocScan result;
	std::vector<std::string> script_bodies;

	void Run(const std::string& text)
	{
		stack.push_back({"#root", "", {}});
		size_t i = 0;
		while (i < text.size())
		{
			size_t lt = text.find('<', i);
			if (lt == std::string::npos)
			{
				ScanText(text.substr(i));
				break;
			}
			ScanText(text.substr(i, lt - i));
			if (text.compare(lt, 4, "<!--") == 0)
			{
				size_t end = text.find("-->", lt + 4);
				i = (end == std::string::npos) ? text.size() : end + 3;
				continue;
			}
			i = ScanTag(text, lt);
		}
	}

private:
	std::vector<Scope> stack;

	size_t ScanTag(const std::string& text, size_t lt)
	{
		size_t i = lt + 1;
		bool closing = false;
		if (i < text.size() && text[i] == '/')
		{
			closing = true;
			i++;
		}
		size_t name_start = i;
		while (i < text.size() && (std::isalnum((unsigned char)text[i]) || text[i] == '-' || text[i] == '_' || text[i] == ':'))
			i++;
		std::string name = Lower(text.substr(name_start, i - name_start));
		if (name.empty())
			return lt + 1;

		// attributes
		std::vector<std::pair<std::string, std::string>> attrs;
		bool self_close = false;
		while (i < text.size() && text[i] != '>')
		{
			if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '>')
			{
				self_close = true;
				i++;
				continue;
			}
			if (std::isspace((unsigned char)text[i]))
			{
				i++;
				continue;
			}
			size_t a = i;
			while (i < text.size() && !std::isspace((unsigned char)text[i]) && text[i] != '=' && text[i] != '>' && text[i] != '/')
				i++;
			std::string an = Lower(text.substr(a, i - a));
			std::string av;
			while (i < text.size() && std::isspace((unsigned char)text[i]))
				i++;
			if (i < text.size() && text[i] == '=')
			{
				i++;
				while (i < text.size() && std::isspace((unsigned char)text[i]))
					i++;
				if (i < text.size() && (text[i] == '"' || text[i] == '\''))
				{
					char q = text[i++];
					size_t s = i;
					while (i < text.size() && text[i] != q)
						i++;
					av = text.substr(s, i - s);
					i++;
				}
				else
				{
					size_t s = i;
					while (i < text.size() && !std::isspace((unsigned char)text[i]) && text[i] != '>')
						i++;
					av = text.substr(s, i - s);
				}
			}
			if (an.empty())
			{
				i++;
				continue;
			}
			attrs.push_back({an, DecodeEntities(av)});
		}
		size_t end = (i < text.size()) ? i + 1 : text.size();

		if (closing)
		{
			for (size_t k = stack.size(); k-- > 1;)
				if (stack[k].tag == name)
				{
					stack.resize(k);
					break;
				}
			return end;
		}

		// Styles are skipped. Scripts are scanned separately: Lua often builds markup (with data-model) at runtime.
		if (name == "script" || name == "style")
		{
			if (name == "script")
				for (auto& [k, v] : attrs)
					if (k == "src" && !v.empty())
						result.scripts.push_back(v);
			if (self_close)
				return end;
			size_t close = Lower(text.substr(end)).find("</" + name);
			size_t stop = close == std::string::npos ? text.size() : end + close;
			if (name == "script")
				script_bodies.push_back(text.substr(end, stop - end));
			return stop;
		}

		if (name == "link")
		{
			std::string type, href;
			for (auto& [k, v] : attrs)
			{
				if (k == "type")
					type = Lower(v);
				if (k == "href")
					href = v;
			}
			if (type == "text/template" && !href.empty())
				result.templates.push_back(href);
			else if ((type == "text/rcss" || type == "text/css") && !href.empty())
				result.stylesheets.push_back(href);
		}

		Scope scope = stack.back();
		scope.tag = name;
		for (auto& [k, v] : attrs)
			if (k == "data-model")
			{
				scope.model = v;
				result.models[v]; // make sure it exists even with no variables
			}
		if (name == "body")
			for (auto& [k, v] : attrs)
				if (k == "id" && result.body_id.empty())
					result.body_id = v;

		// data-for first: the alias it defines applies to this element's other attributes too.
		for (auto& [k, v] : attrs)
			if (k == "data-for")
				AddFor(scope, v);
		for (auto& [k, v] : attrs)
			if (k.rfind("data-alias-", 0) == 0)
			{
				auto toks = Tokenize(v);
				std::vector<std::string> path;
				size_t p = 0;
				if (!toks.empty() && toks[0].t == T::Ident && ReadChain(toks, p, scope, path, nullptr))
					scope.aliases[k.substr(11)] = path;
			}

		stack.push_back(scope);
		for (auto& [k, v] : attrs)
		{
			if (k.rfind("data-", 0) != 0 || k == "data-model" || k == "data-for" || k.rfind("data-alias-", 0) == 0)
				continue;
			VarHint hint = VarHint::Text;
			bool event = false;
			if (k == "data-if" || k == "data-visible" || k == "data-checked" || k.rfind("data-class-", 0) == 0 || k.rfind("data-attrif-", 0) == 0)
				hint = VarHint::Bool;
			else if (k.rfind("data-attr-", 0) == 0)
				hint = VarHint::Number;
			else if (k.rfind("data-style-", 0) == 0 || k == "data-value")
				hint = VarHint::String;
			else if (k.rfind("data-event-", 0) == 0)
			{
				hint = VarHint::None;
				event = true;
			}
			Expression(v, hint, event);
		}
		if (self_close || IsVoid(name))
			stack.pop_back();
		return end;
	}

	static bool IsVoid(const std::string& n)
	{
		return n == "br" || n == "img" || n == "input" || n == "link" || n == "hr" || n == "meta" || n == "col";
	}

	void AddFor(Scope& scope, const std::string& value)
	{
		std::string iter = value, left;
		size_t colon = value.find(':');
		if (colon != std::string::npos)
		{
			left = value.substr(0, colon);
			iter = value.substr(colon + 1);
		}
		auto toks = Tokenize(iter);
		std::vector<std::string> path;
		size_t p = 0;
		if (toks.empty() || toks[0].t != T::Ident || !ReadChain(toks, p, scope, path, nullptr))
			return;
		Record(scope.model, path, VarHint::IsArray);
		std::vector<std::string> elem = path;
		elem.push_back("[]");
		std::string alias = "it", index = "it_index";
		if (!left.empty())
		{
			auto lt = Tokenize(left);
			std::vector<std::string> names;
			for (auto& t : lt)
				if (t.t == T::Ident)
					names.push_back(t.s);
			if (!names.empty())
				alias = names[0];
			index = names.size() > 1 ? names[1] : alias + "_index";
		}
		scope.aliases[alias] = elem;
		scope.aliases[index] = {};
	}

	// Reads ident(.ident | [expr])* starting at toks[p]; resolves aliases. Returns false for keywords/index aliases.
	bool ReadChain(const std::vector<Tok>& toks, size_t& p, const Scope& scope, std::vector<std::string>& path, bool* bracket_seen)
	{
		const std::string first = toks[p].s;
		p++;
		bool valid = true;
		if (first == "true" || first == "false" || first == "ev" || first == "null")
			valid = false;
		auto al = scope.aliases.find(first);
		if (al != scope.aliases.end())
		{
			if (al->second.empty())
				valid = false; // index alias
			path = al->second;
		}
		else
			path = {first};
		while (p < toks.size())
		{
			if (toks[p].t == T::Dot && p + 1 < toks.size() && toks[p + 1].t == T::Ident)
			{
				path.push_back(toks[p + 1].s);
				p += 2;
			}
			else if (toks[p].t == T::LBracket)
			{
				int depth = 1;
				size_t s = ++p;
				while (p < toks.size() && depth > 0)
				{
					if (toks[p].t == T::LBracket)
						depth++;
					else if (toks[p].t == T::RBracket)
						depth--;
					if (depth > 0)
						p++;
				}
				std::vector<Tok> inner(toks.begin() + s, toks.begin() + std::min(p, toks.size()));
				Analyse(inner, scope, VarHint::Number, false);
				p++;
				path.push_back("[]");
				if (bracket_seen)
					*bracket_seen = true;
			}
			else
				break;
		}
		return valid;
	}

	void Expression(const std::string& expr, VarHint context, bool event)
	{
		Analyse(Tokenize(expr), stack.back(), context, event);
	}

	void Analyse(const std::vector<Tok>& toks, const Scope& scope, VarHint context, bool event)
	{
		for (size_t p = 0; p < toks.size();)
		{
			if (toks[p].t != T::Ident)
			{
				p++;
				continue;
			}
			const bool after_pipe = p > 0 && toks[p - 1].t == T::Pipe;
			const bool after_not = p > 0 && toks[p - 1].t == T::Op && toks[p - 1].s == "!";
			const bool after_arith = p > 0 && toks[p - 1].t == T::Op && IsArithmetic(toks[p - 1].s);
			const bool after_cmp_op = p > 1 && toks[p - 1].t == T::Op && IsComparison(toks[p - 1].s);
			const VarHint cmp_hint = after_cmp_op ? (toks[p - 2].t == T::String ? VarHint::String : toks[p - 2].t == T::Number ? VarHint::Number : VarHint::None) : VarHint::None;
			const size_t start = p;
			std::vector<std::string> path;
			bool valid = ReadChain(toks, p, scope, path, nullptr);

			// function call or transform
			const bool call = (p < toks.size() && toks[p].t == T::LParen && path.size() == 1 && p == start + 1);
			if (after_pipe || call)
			{
				if (scope.model.empty())
					continue;
				if (event && !after_pipe)
					result.models[scope.model].events.insert(toks[start].s);
				else
					result.models[scope.model].transforms.insert(toks[start].s);
				continue;
			}
			if (!valid)
				continue;

			VarHint hint = context;
			std::string literal;
			if (after_cmp_op && toks[start - 2].t == T::Number) // 0 == mode
				literal = toks[start - 2].s;
			if (!path.empty() && path.back() == "size" && path.size() > 1)
			{
				path.pop_back();
				hint = VarHint::IsArray;
			}
			else if (after_not)
				hint = VarHint::Bool;
			else if (cmp_hint != VarHint::None)
				hint = cmp_hint;
			else if (after_arith)
				hint = VarHint::Number;
			else if (p < toks.size() && toks[p].t == T::Op)
			{
				const std::string& op = toks[p].s;
				if (IsComparison(op) && p + 1 < toks.size())
				{
					if (toks[p + 1].t == T::Number || (toks[p + 1].t == T::Op && toks[p + 1].s == "-"))
					{
						hint = VarHint::Number;
						if (toks[p + 1].t == T::Number)
							literal = toks[p + 1].s;
						else if (p + 2 < toks.size() && toks[p + 2].t == T::Number)
							literal = "-" + toks[p + 2].s;
					}
					else if (toks[p + 1].t == T::String)
						hint = VarHint::String;
					else
						hint = VarHint::Number;
				}
				else if (IsArithmetic(op))
					hint = VarHint::Number;
				else if (op == "?" || op == "&&" || op == "||")
					hint = VarHint::Bool;
				else if (op == "=" && event)
					hint = VarHint::None;
			}
			Record(scope.model, path, hint, literal);
		}
	}

	void Record(const std::string& model, const std::vector<std::string>& path, VarHint hint, const std::string& literal = "")
	{
		if (model.empty() || path.empty())
			return;
		result.models[model].vars.push_back({path, hint, literal});
	}

	void ScanText(const std::string& text)
	{
		size_t i = 0;
		while ((i = text.find("{{", i)) != std::string::npos)
		{
			size_t e = text.find("}}", i + 2);
			if (e == std::string::npos)
				break;
			Expression(DecodeEntities(text.substr(i + 2, e - i - 2)), VarHint::Text, false);
			i = e + 2;
		}
	}
};

} // namespace

DocScan ScanDocument(const std::string& rml)
{
	Scanner s;
	s.Run(rml);
	DocScan result = s.result;
	for (const auto& body : s.script_bodies)
	{
		Scanner sub;
		sub.Run(body);
		for (auto& [name, m] : sub.result.models)
		{
			auto& dst = result.models[name];
			dst.vars.insert(dst.vars.end(), m.vars.begin(), m.vars.end());
			dst.events.insert(m.events.begin(), m.events.end());
			dst.transforms.insert(m.transforms.begin(), m.transforms.end());
		}
	}
	return result;
}
