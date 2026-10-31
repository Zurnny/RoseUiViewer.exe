#pragma once
// Maps the paths the game UI uses ("/data/ui/themes/rose/...", "Data/ui/...",
// relative paths) onto the ROSE Online folder on disk.
#include <RmlUi/Core/FileInterface.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace GameFiles {

// The ROSE Online folder (the one that holds Data\).
void SetRoot(const fs::path& root);
const fs::path& Root();

// Turns a UI path into a real file path. Case is ignored, so "/data/ui" finds "Data\ui".
fs::path Resolve(const std::string& ui_path);

// Path of a real file relative to the game folder, with forward slashes ("Data/ui/...").
std::string ToUiPath(const fs::path& real);

bool ReadAll(const fs::path& file, std::string& out);
bool WriteAll(const fs::path& file, const std::string& data);

// True when 'folder' looks like a ROSE Online install (has Data/ui/themes).
bool LooksLikeGameFolder(const fs::path& folder);

// Walks up from a file (e.g. a dropped hud.html) to the game folder, or returns empty.
fs::path FindGameFolderAbove(const fs::path& file);

std::string Lower(std::string s);

} // namespace GameFiles

class GameFileInterface : public Rml::FileInterface {
public:
	Rml::FileHandle Open(const Rml::String& path) override;
	void Close(Rml::FileHandle file) override;
	size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
	bool Seek(Rml::FileHandle file, long offset, int origin) override;
	size_t Tell(Rml::FileHandle file) override;
	size_t Length(Rml::FileHandle file) override;
	bool LoadFile(const Rml::String& path, Rml::String& out_data) override;
};
