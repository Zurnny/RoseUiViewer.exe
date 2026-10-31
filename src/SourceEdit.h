#pragma once
// Text-level edits to RML/RCSS files that keep the rest of the file exactly as it was.
#include <string>
#include <utility>
#include <vector>

namespace SourceEdit {

using Decls = std::vector<std::pair<std::string, std::string>>; // property, value ("" = remove)

// Adds viewer-src="<file_index>:<offset>" to every opening tag (outside scripts, styles and comments),
// so an element on screen can be traced back to the exact tag in the file. Offsets are into 'text'.
std::string Annotate(const std::string& text, int file_index);

struct Tag {
	size_t start = 0, end = 0; // [start, end) covers "<name ... >"
	std::string name;
};
bool FindTag(const std::string& text, size_t offset, Tag& out);
bool GetAttribute(const std::string& text, const Tag& tag, const std::string& name, std::string& value);
// Sets (or, with an empty value and remove=true, removes) an attribute on the tag at 'offset'.
bool SetAttribute(std::string& text, size_t offset, const std::string& name, const std::string& value, bool remove = false);

// Updates 'selector { ... }' in the document's <style> in <head>: edits an existing rule with exactly that
// selector, or adds it in a "ROSE UI Viewer edits" section (creating a <style> if there is none).
bool UpsertDocumentRule(std::string& html, const std::string& selector, const Decls& decls);

// Edits the declarations of the rule that starts on 'line' (1-based) in a .css file or an .html <style>.
bool UpsertRuleAtLine(std::string& text, int line, const Decls& decls);

void OffsetToLineCol(const std::string& text, size_t offset, int& line, int& col); // 0-based
size_t LineColToOffset(const std::string& text, int line, int col);

} // namespace SourceEdit
