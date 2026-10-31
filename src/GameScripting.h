#pragma once
// Makes scripts and event attributes behave the way the ROSE client runs them:
//  - each window's <script> code sees its own 'document' (other globals stay shared between windows),
//  - onclick="close" style attributes are game commands, not Lua: the viewer logs them
//    (and "close" hides the window).
void InstallGameScripting();
