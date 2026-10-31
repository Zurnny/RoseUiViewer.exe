#pragma once
// Makes scripts and event attributes behave the way the ROSE client runs them:
//  - each window's <script> code runs after the window is built and sees its own 'document'
//    (other globals stay shared between windows),
//  - onclick="close" style attributes are game commands, not Lua: the viewer logs them
//    (and "close" hides the window).
void InstallGameScripting();

namespace Rml {
class ElementDocument;
}
// Runs a window's <script> code, in order, once the whole window has been built (as the game does),
// so scripts can find their elements with GetElementById.
void RunDeferredScripts(Rml::ElementDocument* doc);

// Game commands of the form "name:value" (onclick="mode:1", "bag:0") set a data model value in the game.
// The viewer passes them here; return true when handled.
#include <functional>
#include <string>
namespace Rml {
class Element;
}
void SetCommandHandler(std::function<bool(Rml::Element* element, const std::string& name, const std::string& value)> handler);
