#include "EditorLanguages.h"
#include <algorithm>
#include <cctype>

using PI = TextEditor::PaletteIndex;

static std::string LowerExt(const std::string& path)
{
	size_t dot = path.find_last_of('.');
	std::string e = dot == std::string::npos ? "" : path.substr(dot + 1);
	std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return e;
}

static const TextEditor::LanguageDefinition& Html()
{
	static TextEditor::LanguageDefinition d;
	static bool done = false;
	if (!done)
	{
		done = true;
		d.mName = "RML";
		d.mCommentStart = "<!--";
		d.mCommentEnd = "-->";
		d.mSingleLineComment = "\x01"; // none
		d.mPreprocChar = '\x01';
		d.mCaseSensitive = false;
		d.mAutoIndentation = true;
		d.mTokenRegexStrings = {
			{R"(\{\{[^}]*\}\})", PI::Preprocessor},             // {{ binding }}
			{R"(<\/?[a-zA-Z][a-zA-Z0-9_\-:]*)", PI::Keyword},   // <tag
			{R"(\/?>)", PI::Keyword},                           // > />
			{R"(data-[a-zA-Z0-9_\-]+)", PI::PreprocIdentifier}, // data bindings
			{R"([a-zA-Z_][a-zA-Z0-9_\-:]*(?=\s*=))", PI::KnownIdentifier}, // attribute names
			{R"("[^"]*"?)", PI::String},
			{R"('[^']*'?)", PI::String},
			{R"(#[0-9a-fA-F]{3,8}\b)", PI::Number},
			{R"([+-]?[0-9]+(\.[0-9]+)?(dp|px|%|em|deg|s|ms)?)", PI::Number},
			{R"([a-zA-Z_][a-zA-Z0-9_\-]*)", PI::Identifier},
			{R"([\[\]\{\}\!\%\^\&\*\(\)\-\+\=\~\|\<\>\?\/\;\,\.\:#])", PI::Punctuation},
		};
	}
	return d;
}

static const TextEditor::LanguageDefinition& Css()
{
	static TextEditor::LanguageDefinition d;
	static bool done = false;
	if (!done)
	{
		done = true;
		d.mName = "RCSS";
		d.mCommentStart = "/*";
		d.mCommentEnd = "*/";
		d.mSingleLineComment = "\x01";
		d.mPreprocChar = '\x01';
		d.mCaseSensitive = false;
		d.mAutoIndentation = true;
		d.mTokenRegexStrings = {
			{R"(@[a-zA-Z\-]+)", PI::Preprocessor},
			{R"([a-zA-Z\-]+(?=\s*:[^:]))", PI::KnownIdentifier}, // property:
			{R"([#\.][a-zA-Z_][a-zA-Z0-9_\-]*)", PI::Keyword},   // #id .class
			{R"(#[0-9a-fA-F]{3,8}\b)", PI::Number},
			{R"("[^"]*"?)", PI::String},
			{R"('[^']*'?)", PI::String},
			{R"([+-]?[0-9]+(\.[0-9]+)?(dp|px|%|em|deg|s|ms|x)?)", PI::Number},
			{R"(:[a-zA-Z\-]+)", PI::PreprocIdentifier}, // :hover
			{R"([a-zA-Z_][a-zA-Z0-9_\-]*)", PI::Identifier},
			{R"([\[\]\{\}\!\%\^\&\*\(\)\-\+\=\~\|\<\>\?\/\;\,\.\:])", PI::Punctuation},
		};
	}
	return d;
}

const TextEditor::LanguageDefinition& LanguageForFile(const std::string& path)
{
	const std::string e = LowerExt(path);
	if (e == "css" || e == "rcss")
		return Css();
	if (e == "lua")
		return TextEditor::LanguageDefinition::Lua();
	return Html();
}

const TextEditor::Palette& EditorPalette()
{
	static TextEditor::Palette p = [] {
		TextEditor::Palette q = TextEditor::GetDarkPalette();
		auto set = [&](PI i, unsigned rgba) { q[(int)i] = rgba; }; // ImU32 is ABGR
		set(PI::Default, 0xffd6dde3);
		set(PI::Keyword, 0xff5fb0e8);           // tags / selectors: warm orange
		set(PI::Number, 0xff8cd3b5);            // numbers: green
		set(PI::String, 0xff7cc4e6);            // strings: tan
		set(PI::Punctuation, 0xffa0a8b0);
		set(PI::Preprocessor, 0xffd78fd0);      // {{ }} and @rules: pink
		set(PI::Identifier, 0xffd6dde3);
		set(PI::KnownIdentifier, 0xffe8c47c);   // attribute/property names: light blue
		set(PI::PreprocIdentifier, 0xffd7a86c); // data-* / :hover
		set(PI::Comment, 0xff6f8a6a);
		set(PI::MultiLineComment, 0xff6f8a6a);
		set(PI::Background, 0xff1c1a18);
		set(PI::CurrentLineFill, 0x22ffffff);
		set(PI::CurrentLineFillInactive, 0x10ffffff);
		set(PI::CurrentLineEdge, 0x30ffffff);
		set(PI::LineNumber, 0xff6c757d);
		set(PI::Selection, 0x80a06030);
		return q;
	}();
	return p;
}
