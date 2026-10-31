#include "SourceEdit.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace SourceEdit {

static std::string Lower(std::string s)
{
	for (auto& c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

static bool NameChar(char c) { return std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':'; }

// Skips a tag starting at text[i] == '<' and returns the index just past '>' (quotes respected).
static size_t SkipTag(const std::string& text, size_t i)
{
	char q = 0;
	for (size_t k = i + 1; k < text.size(); k++)
	{
		char c = text[k];
		if (q)
		{
			if (c == q)
				q = 0;
		}
		else if (c == '"' || c == '\'')
			q = c;
		else if (c == '>')
			return k + 1;
	}
	return text.size();
}

std::string Annotate(const std::string& text, int file_index)
{
	std::string out;
	out.reserve(text.size() + text.size() / 8);
	const std::string prefix = " viewer-src=\"" + std::to_string(file_index) + ":";
	size_t i = 0;
	while (i < text.size())
	{
		size_t lt = text.find('<', i);
		if (lt == std::string::npos)
		{
			out.append(text, i, std::string::npos);
			break;
		}
		out.append(text, i, lt - i);
		if (text.compare(lt, 4, "<!--") == 0)
		{
			size_t e = text.find("-->", lt + 4);
			e = (e == std::string::npos) ? text.size() : e + 3;
			out.append(text, lt, e - lt);
			i = e;
			continue;
		}
		size_t n = lt + 1;
		if (n < text.size() && (text[n] == '/' || text[n] == '!' || text[n] == '?' || !std::isalpha((unsigned char)text[n])))
		{
			out += '<';
			i = lt + 1;
			continue;
		}
		size_t ne = n;
		while (ne < text.size() && NameChar(text[ne]))
			ne++;
		const std::string name = Lower(text.substr(n, ne - n));
		size_t end = SkipTag(text, lt);
		static const char* skip[] = {"rml", "head", "title", "link", "meta", "style", "script", "template"};
		bool plain = std::find_if(std::begin(skip), std::end(skip), [&](const char* s) { return name == s; }) != std::end(skip);
		out.append(text, lt, ne - lt);
		if (!plain)
			out += prefix + std::to_string(lt) + "\"";
		out.append(text, ne, end - ne);
		i = end;
		if (name == "script" || name == "style")
		{
			bool self_closing = end >= 2 && text[end - 2] == '/';
			if (!self_closing)
			{
				size_t close = Lower(text.substr(end)).find("</" + name);
				size_t stop = close == std::string::npos ? text.size() : end + close;
				out.append(text, end, stop - end);
				i = stop;
			}
		}
	}
	return out;
}

bool FindTag(const std::string& text, size_t offset, Tag& out)
{
	if (offset >= text.size() || text[offset] != '<')
		return false;
	size_t n = offset + 1, ne = n;
	while (ne < text.size() && NameChar(text[ne]))
		ne++;
	if (ne == n)
		return false;
	out.start = offset;
	out.end = SkipTag(text, offset);
	out.name = text.substr(n, ne - n);
	return true;
}

// Finds attribute 'name' inside the tag: returns [name_start, value_end) and value range.
static bool LocateAttr(const std::string& text, const Tag& tag, const std::string& name, size_t& a_start, size_t& a_end, size_t& v_start, size_t& v_end)
{
	size_t i = tag.start + 1 + tag.name.size();
	const std::string want = Lower(name);
	while (i < tag.end)
	{
		while (i < tag.end && (std::isspace((unsigned char)text[i]) || text[i] == '/'))
			i++;
		if (i >= tag.end || text[i] == '>')
			break;
		size_t s = i;
		while (i < tag.end && !std::isspace((unsigned char)text[i]) && text[i] != '=' && text[i] != '>' && text[i] != '/')
			i++;
		std::string an = Lower(text.substr(s, i - s));
		size_t ws = i;
		while (ws < tag.end && std::isspace((unsigned char)text[ws]))
			ws++;
		size_t vs = i, ve = i;
		if (ws < tag.end && text[ws] == '=')
		{
			i = ws + 1;
			while (i < tag.end && std::isspace((unsigned char)text[i]))
				i++;
			if (i < tag.end && (text[i] == '"' || text[i] == '\''))
			{
				char q = text[i];
				vs = i + 1;
				size_t e = text.find(q, vs);
				ve = (e == std::string::npos || e > tag.end) ? tag.end : e;
				i = ve + 1;
			}
			else
			{
				vs = i;
				while (i < tag.end && !std::isspace((unsigned char)text[i]) && text[i] != '>')
					i++;
				ve = i;
			}
		}
		if (an == want)
		{
			a_start = s;
			a_end = i;
			v_start = vs;
			v_end = ve;
			return true;
		}
		if (i == s)
			i++;
	}
	return false;
}

bool GetAttribute(const std::string& text, const Tag& tag, const std::string& name, std::string& value)
{
	size_t as, ae, vs, ve;
	if (!LocateAttr(text, tag, name, as, ae, vs, ve))
		return false;
	value = text.substr(vs, ve - vs);
	return true;
}

static std::string EscapeAttr(const std::string& v)
{
	std::string o;
	for (char c : v)
	{
		if (c == '"')
			o += "&quot;";
		else if (c == '&')
			o += "&amp;";
		else if (c == '<')
			o += "&lt;";
		else
			o += c;
	}
	return o;
}

bool SetAttribute(std::string& text, size_t offset, const std::string& name, const std::string& value, bool remove)
{
	Tag tag;
	if (!FindTag(text, offset, tag))
		return false;
	size_t as, ae, vs, ve;
	if (LocateAttr(text, tag, name, as, ae, vs, ve))
	{
		if (remove)
		{
			size_t s = as;
			while (s > tag.start && std::isspace((unsigned char)text[s - 1]))
				s--;
			text.erase(s, ae - s);
		}
		else
			text.replace(as, ae - as, name + "=\"" + EscapeAttr(value) + "\"");
		return true;
	}
	if (remove)
		return true;
	size_t at = tag.start + 1 + tag.name.size();
	text.insert(at, " " + name + "=\"" + EscapeAttr(value) + "\"");
	return true;
}

// ---------------------------------------------------------------- style rules

static std::string Trim(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a]))
		a++;
	while (b > a && std::isspace((unsigned char)s[b - 1]))
		b--;
	return s.substr(a, b - a);
}

static std::string CollapseSpaces(const std::string& s)
{
	std::string o;
	bool sp = false;
	for (char c : Trim(s))
	{
		if (std::isspace((unsigned char)c))
		{
			sp = true;
			continue;
		}
		if (sp && !o.empty())
			o += ' ';
		sp = false;
		o += c;
	}
	return o;
}

// Matching '}' for the '{' at 'open', skipping comments and strings.
static size_t MatchBrace(const std::string& t, size_t open)
{
	int depth = 0;
	for (size_t i = open; i < t.size(); i++)
	{
		if (t.compare(i, 2, "/*") == 0)
		{
			size_t e = t.find("*/", i + 2);
			if (e == std::string::npos)
				return std::string::npos;
			i = e + 1;
			continue;
		}
		char c = t[i];
		if (c == '"' || c == '\'')
		{
			size_t e = t.find(c, i + 1);
			if (e == std::string::npos)
				return std::string::npos;
			i = e;
			continue;
		}
		if (c == '{')
			depth++;
		else if (c == '}' && --depth == 0)
			return i;
	}
	return std::string::npos;
}

// Edits declarations inside t[open+1 .. close).
static void UpsertDecls(std::string& t, size_t open, size_t close, const Decls& decls)
{
	for (const auto& [prop_in, value] : decls)
	{
		const std::string prop = Lower(Trim(prop_in));
		// Walk declarations: [start, end) of each "name: value;" (end includes ';' if present).
		size_t i = open + 1;
		bool done = false;
		while (i < close)
		{
			while (i < close && (std::isspace((unsigned char)t[i]) || t[i] == ';'))
				i++;
			if (i >= close)
				break;
			if (t.compare(i, 2, "/*") == 0)
			{
				size_t e = t.find("*/", i + 2);
				i = (e == std::string::npos || e > close) ? close : e + 2;
				continue;
			}
			size_t ds = i;
			size_t colon = std::string::npos;
			char q = 0;
			int paren = 0;
			while (i < close)
			{
				char c = t[i];
				if (q)
				{
					if (c == q)
						q = 0;
				}
				else if (c == '"' || c == '\'')
					q = c;
				else if (c == '(')
					paren++;
				else if (c == ')')
					paren--;
				else if (c == ':' && colon == std::string::npos)
					colon = i;
				else if (c == ';' && paren <= 0)
					break;
				i++;
			}
			size_t de = (i < close) ? i + 1 : i; // include ';'
			if (colon != std::string::npos && Lower(Trim(t.substr(ds, colon - ds))) == prop)
			{
				if (value.empty())
				{
					size_t s = ds;
					while (s > open + 1 && (t[s - 1] == ' ' || t[s - 1] == '\t'))
						s--;
					size_t e = de;
					// remove the whole line when the declaration is alone on it
					bool line_start = s == 0 || t[s - 1] == '\n';
					if (line_start && e < t.size() && t[e] == '\n')
						e++;
					else if (line_start && e < t.size() && t[e] == '\r' && e + 1 < t.size() && t[e + 1] == '\n')
						e += 2;
					t.erase(s, e - s);
					close -= (e - s);
				}
				else
				{
					size_t vs = colon + 1;
					size_t ve = (i < close) ? i : i;
					// keep any !important? (replace value fully)
					std::string repl = " " + value;
					// trim trailing spaces before ';' end
					while (ve > vs && std::isspace((unsigned char)t[ve - 1]))
						ve--;
					t.replace(vs, ve - vs, repl);
					long delta = (long)repl.size() - (long)(ve - vs);
					close = (size_t)((long)close + delta);
					if (i >= close - delta && t[close - 1] != ';')
					{
						// last declaration had no ';' - fine
					}
				}
				done = true;
				break;
			}
			i = de;
		}
		if (done || value.empty())
			continue;
		// Append a new declaration.
		std::string body = t.substr(open + 1, close - open - 1);
		const bool multiline = body.find('\n') != std::string::npos;
		// Make sure the last declaration ends with ';'
		size_t last = close;
		while (last > open + 1 && std::isspace((unsigned char)t[last - 1]))
			last--;
		if (last > open + 1 && t[last - 1] != ';' && t[last - 1] != '{' && !(last >= 2 && t.compare(last - 2, 2, "*/") == 0))
		{
			t.insert(last, ";");
			close++;
			last++;
		}
		if (multiline)
		{
			// indent like the line containing the last declaration (or the brace line + tab)
			size_t ls = t.rfind('\n', last > 0 ? last - 1 : 0);
			std::string indent;
			if (ls != std::string::npos && ls > open)
			{
				for (size_t k = ls + 1; k < t.size() && (t[k] == ' ' || t[k] == '\t'); k++)
					indent += t[k];
			}
			else
			{
				size_t bs = t.rfind('\n', open);
				bs = bs == std::string::npos ? 0 : bs + 1;
				for (size_t k = bs; k < t.size() && (t[k] == ' ' || t[k] == '\t'); k++)
					indent += t[k];
				indent += "\t";
			}
			std::string ins = "\n" + indent + prop + ": " + value + ";";
			t.insert(last, ins);
			close += ins.size();
		}
		else
		{
			std::string ins = " " + prop + ": " + value + ";";
			// insert before trailing spaces preceding '}'
			t.insert(last, ins);
			close += ins.size();
		}
	}
}

static const char* kSectionMarker = "/* ==== ROSE UI Viewer edits ==== */";

// Finds the rule with exactly 'selector' between [from, to); returns position of '{' or npos.
static size_t FindRule(const std::string& t, size_t from, size_t to, const std::string& selector)
{
	const std::string want = CollapseSpaces(selector);
	size_t i = from, seg = from;
	while (i < to)
	{
		if (t.compare(i, 2, "/*") == 0)
		{
			size_t e = t.find("*/", i + 2);
			i = (e == std::string::npos) ? to : e + 2;
			seg = i;
			continue;
		}
		if (t[i] == '{')
		{
			std::string sel = CollapseSpaces(t.substr(seg, i - seg));
			if (sel == want)
				return i;
			size_t close = MatchBrace(t, i);
			if (close == std::string::npos || close >= to)
				return std::string::npos;
			i = close + 1;
			seg = i;
			continue;
		}
		if (t[i] == '}' || t[i] == ';')
			seg = i + 1;
		i++;
	}
	return std::string::npos;
}

bool UpsertDocumentRule(std::string& html, const std::string& selector, const Decls& decls)
{
	const std::string low = Lower(html);
	size_t head_end = low.find("</head>");
	size_t body = low.find("<body");
	if (head_end == std::string::npos)
		head_end = body;
	if (head_end == std::string::npos)
		return false;

	// All <style> blocks in the head.
	std::vector<std::pair<size_t, size_t>> styles; // content [start, end)
	size_t p = 0;
	while ((p = low.find("<style", p)) != std::string::npos && p < head_end)
	{
		size_t cs = low.find('>', p);
		if (cs == std::string::npos)
			break;
		cs++;
		size_t ce = low.find("</style", cs);
		if (ce == std::string::npos)
			break;
		styles.push_back({cs, ce});
		p = ce + 1;
	}

	// 1. An existing rule with exactly this selector.
	for (auto it = styles.rbegin(); it != styles.rend(); ++it)
	{
		size_t brace = FindRule(html, it->first, it->second, selector);
		if (brace != std::string::npos)
		{
			size_t close = MatchBrace(html, brace);
			if (close == std::string::npos)
				return false;
			UpsertDecls(html, brace, close, decls);
			return true;
		}
	}

	// Nothing to add if every declaration is a removal.
	bool any = false;
	for (auto& d : decls)
		any |= !d.second.empty();
	if (!any)
		return true;

	std::string decl_text;
	for (auto& [k, v] : decls)
		if (!v.empty())
			decl_text += " " + Lower(Trim(k)) + ": " + v + ";";
	const std::string rule = selector + " {" + decl_text + " }";

	// 2. Add it to the viewer section of the last <style>, creating the section if needed.
	if (!styles.empty())
	{
		auto [cs, ce] = styles.back();
		// indentation of the line holding </style>
		size_t ls = html.rfind('\n', ce > 0 ? ce - 1 : 0);
		std::string indent;
		for (size_t k = (ls == std::string::npos ? 0 : ls + 1); k < ce && (html[k] == ' ' || html[k] == '\t'); k++)
			indent += html[k];
		// one more level, in the file's own style (tabs or spaces)
		std::string inner = indent + (indent.find('\t') != std::string::npos || indent.empty() ? "\t" : "    ");
		size_t insert_at = ce;
		// back up over whitespace before </style>
		while (insert_at > cs && (html[insert_at - 1] == ' ' || html[insert_at - 1] == '\t'))
			insert_at--;
		std::string ins;
		if (html.find(kSectionMarker, cs) == std::string::npos || html.find(kSectionMarker, cs) > ce)
			ins += "\n" + inner + kSectionMarker;
		ins += "\n" + inner + rule + "\n";
		// avoid a blank line pile-up: if the char before insert_at is '\n', drop our leading newline
		if (insert_at > 0 && html[insert_at - 1] == '\n')
			ins.erase(0, 1);
		html.insert(insert_at, ins);
		return true;
	}

	// 3. No <style> at all: add one before </head>.
	std::string ins = "\t<style>\n\t\t" + std::string(kSectionMarker) + "\n\t\t" + rule + "\n\t</style>\n";
	html.insert(head_end, ins);
	return true;
}

bool UpsertRuleAtLine(std::string& text, int line, const Decls& decls)
{
	size_t off = LineColToOffset(text, line - 1, 0);
	if (off >= text.size())
		return false;
	size_t brace = text.find('{', off);
	size_t stop = text.find('}', off);
	if (brace == std::string::npos || (stop != std::string::npos && stop < brace))
	{
		// The line may be inside the rule; walk back to its '{'.
		size_t b = text.rfind('{', off);
		if (b == std::string::npos)
			return false;
		brace = b;
	}
	size_t close = MatchBrace(text, brace);
	if (close == std::string::npos)
		return false;
	UpsertDecls(text, brace, close, decls);
	return true;
}

void OffsetToLineCol(const std::string& text, size_t offset, int& line, int& col)
{
	line = 0;
	col = 0;
	for (size_t i = 0; i < offset && i < text.size(); i++)
	{
		if (text[i] == '\n')
		{
			line++;
			col = 0;
		}
		else
			col++;
	}
}

size_t LineColToOffset(const std::string& text, int line, int col)
{
	size_t i = 0;
	int l = 0;
	while (l < line && i < text.size())
	{
		if (text[i] == '\n')
			l++;
		i++;
	}
	return std::min(text.size(), i + (size_t)std::max(0, col));
}

} // namespace SourceEdit
