#pragma once
// Stand-in for the game's C++ data models: each model's values come from mock/<model>.json.
#include "GameFiles.h"
#include "MarkupScan.h"
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>
#include <vector>

namespace Rml {
class Context;
}

class MockModels {
public:
	void SetFolder(const fs::path& folder) { folder_ = folder; }
	const fs::path& Folder() const { return folder_; }

	// Creates every model in 'models' on the context. Values come from mock/<name>.json; variables the
	// documents use that the file doesn't have yet are added to it with sample values.
	void Build(Rml::Context* context, const std::map<std::string, ModelScan>& models);

	// Removes the models again (all documents must be closed first).
	void RemoveAll(Rml::Context* context);

	fs::path FileFor(const std::string& model) const;

	// Live access for the sample-data editor.
	std::vector<std::string> Names() const;
	nlohmann::json* Data(const std::string& model);
	void Changed(const std::string& model);  // values edited: refresh the windows (no reload needed)
	bool Save(const std::string& model);     // write mock/<model>.json

private:
	fs::path folder_;
	std::map<std::string, std::unique_ptr<nlohmann::json>> data_;
	std::map<std::string, Rml::DataModelHandle> handles_;
};
