#pragma once
// Keeps RoseUiViewer.exe up to date from the GitHub releases of Zurnny/RoseUiViewer.exe.
// Checks in the background, downloads the new exe, checks its SHA-256, and swaps it in
// (Windows lets a running exe be renamed, so the next start is the new version).
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

class Updater {
public:
	enum class State { Idle, Checking, UpToDate, Downloading, Ready, Failed, Unsupported };

	~Updater();

	// exe: the running RoseUiViewer.exe. version: this build's version ("dev" for local builds).
	void Start(const std::filesystem::path& exe, const std::string& version, bool check_now);
	void CheckNow();

	State GetState() const { return state; }
	std::string Version() const { return version; }
	std::string LatestVersion();
	std::string Message();
	float Progress() const { return progress; }

	// Starts the (new) exe again; call after the app has shut down.
	static bool Relaunch(const std::filesystem::path& exe);

	// "1.12" > "1.9"
	static bool IsNewer(const std::string& latest, const std::string& current);

private:
	void Run();
	void SetFailed(const std::string& why);

	std::filesystem::path exe;
	std::string version;
	std::atomic<State> state{State::Idle};
	std::atomic<float> progress{0};
	std::mutex mutex;
	std::string latest, message;
	std::thread worker;
};
