#pragma once
#include "TextEditor.h"
#include <string>

// Syntax colouring for the code editor: RML/HTML, RCSS/CSS and Lua.
const TextEditor::LanguageDefinition& LanguageForFile(const std::string& path);
const TextEditor::Palette& EditorPalette();
