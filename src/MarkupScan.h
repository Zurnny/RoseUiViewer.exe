#pragma once
// Reads an RML document as text and lists what the game's C++ code would have to supply:
// data models, the variables each one uses, event callbacks and transform functions.
#include <map>
#include <set>
#include <string>
#include <vector>

enum class VarHint { None, Bool, Number, String, Text, IsArray };

struct VarUse {
	std::vector<std::string> path; // "[]" marks "an element of this array"
	VarHint hint = VarHint::None;
};

struct ModelScan {
	std::vector<VarUse> vars;
	std::set<std::string> events;
	std::set<std::string> transforms;
};

struct DocScan {
	std::map<std::string, ModelScan> models;
	std::vector<std::string> templates; // hrefs of <link type="text/template">
	std::vector<std::string> scripts;   // src of <script src="...">
	std::vector<std::string> stylesheets; // hrefs of <link type="text/rcss">
	std::string body_id;
};

DocScan ScanDocument(const std::string& rml);
