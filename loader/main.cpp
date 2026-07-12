#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <tlhelp32.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	constexpr wchar_t kDefaultProcess[] = L"PenguinHotel-Win64-Shipping.exe";
	constexpr wchar_t kDefaultInjectDll[] = L"peterhack.dll";

	struct Options
	{
		bool showHelp = false;
		bool localOnly = false;
		bool injectOnly = false;
		bool waitForProcess = false;
		fs::path manifestPath;
		fs::path deployDir;
		std::wstring processName = kDefaultProcess;
		std::wstring injectDll = kDefaultInjectDll;
	};

	struct Manifest
	{
		std::string baseUrl;
		std::wstring processName = kDefaultProcess;
		std::wstring injectDll = kDefaultInjectDll;
		std::vector<std::string> files;
	};

	std::wstring ToWide(std::string_view text)
	{
		if (text.empty())
			return {};
		const int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		if (len <= 0)
			return {};
		std::wstring out(static_cast<size_t>(len), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), len);
		return out;
	}

	std::string ToUtf8(std::wstring_view text)
	{
		if (text.empty())
			return {};
		const int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		if (len <= 0)
			return {};
		std::string out(static_cast<size_t>(len), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
		return out;
	}

	std::wstring DesktopDeployDir()
	{
		wchar_t userProfile[MAX_PATH]{};
		if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) == 0)
			return L"C:\\peterhack";
		return std::wstring(userProfile) + L"\\Desktop\\peterhack";
	}

	std::wstring ExeDirectory()
	{
		wchar_t path[MAX_PATH]{};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring dir(path);
		const size_t slash = dir.find_last_of(L"\\/");
		if (slash != std::wstring::npos)
			dir.resize(slash);
		return dir;
	}

	void Log(const std::wstring& line)
	{
		std::wcout << line << L'\n';
	}

	void LogError(const std::wstring& line)
	{
		std::wcerr << L"[error] " << line << L'\n';
	}

	std::optional<std::string> ReadFileUtf8(const fs::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return std::nullopt;
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	bool WriteFileBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes)
	{
		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return out.good();
	}

	std::optional<std::string> ExtractJsonString(const std::string& json, std::string_view key)
	{
		const std::string needle = std::string("\"") + std::string(key) + "\":\"";
		const size_t pos = json.find(needle);
		if (pos == std::string::npos)
			return std::nullopt;
		size_t start = pos + needle.size();
		size_t end = start;
		while (end < json.size() && json[end] != '"')
		{
			if (json[end] == '\\' && end + 1 < json.size())
				end += 2;
			else
				++end;
		}
		if (end >= json.size())
			return std::nullopt;
		return json.substr(start, end - start);
	}

	std::optional<Manifest> ParseManifest(const std::string& json)
	{
		Manifest manifest;
		if (auto baseUrl = ExtractJsonString(json, "baseUrl"))
			manifest.baseUrl = *baseUrl;
		if (auto process = ExtractJsonString(json, "process"))
			manifest.processName = ToWide(*process);
		if (auto injectDll = ExtractJsonString(json, "injectDll"))
			manifest.injectDll = ToWide(*injectDll);

		const size_t filesPos = json.find("\"files\"");
		if (filesPos == std::string::npos)
			return std::nullopt;
		const size_t arrayStart = json.find('[', filesPos);
		const size_t arrayEnd = json.find(']', arrayStart);
		if (arrayStart == std::string::npos || arrayEnd == std::string::npos || arrayEnd <= arrayStart)
			return std::nullopt;

		size_t cursor = arrayStart + 1;
		while (cursor < arrayEnd)
		{
			cursor = json.find('"', cursor);
			if (cursor == std::string::npos || cursor >= arrayEnd)
				break;
			++cursor;
			size_t end = cursor;
			while (end < arrayEnd && json[end] != '"')
			{
				if (json[end] == '\\' && end + 1 < json.size())
					end += 2;
				else
					++end;
			}
			if (end >= arrayEnd)
				break;
			manifest.files.push_back(json.substr(cursor, end - cursor));
			cursor = end + 1;
		}
		if (manifest.files.empty())
			return std::nullopt;
		return manifest;
	}

	std::optional<Manifest> LoadManifest(const fs::path& path)
	{
		const auto text = ReadFileUtf8(path);
		if (!text)
			return std::nullopt;
		return ParseManifest(*text);
	}

	bool EndsWith(std::string_view value, std::string_view suffix)
	{
		return value.size() >= suffix.size() &&
			value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	std::string JoinUrl(std::string_view base, std::string_view relative)
	{
		std::string out(base);
		if (!out.empty() && out.back() != '/')
			out.push_back('/');
		while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\'))
			relative.remove_prefix(1);
		out.append(relative.begin(), relative.end());
		return out;
	}

	std::optional<std::vector<std::uint8_t>> HttpDownload(const std::wstring& url)
	{
		URL_COMPONENTS parts{};
		parts.dwStructSize = sizeof(parts);
		wchar_t host[256]{};
		wchar_t path[2048]{};
		parts.lpszHostName = host;
		parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
		parts.lpszUrlPath = path;
		parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));

		if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
			return std::nullopt;

		const INTERNET_PORT port = parts.nPort ? parts.nPort :
			(parts.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
		const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

		HINTERNET session = WinHttpOpen(L"peterhack-loader/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (!session)
			return std::nullopt;

		HINTERNET connect = WinHttpConnect(session, host, port, 0);
		if (!connect)
		{
			WinHttpCloseHandle(session);
			return std::nullopt;
		}

		HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			secure ? WINHTTP_FLAG_SECURE : 0);
		if (!request)
		{
			WinHttpCloseHandle(connect);
			WinHttpCloseHandle(session);
			return std::nullopt;
		}

		if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
			!WinHttpReceiveResponse(request, nullptr))
		{
			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connect);
			WinHttpCloseHandle(session);
			return std::nullopt;
		}

		std::vector<std::uint8_t> data;
		for (;;)
		{
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request, &available))
				break;
			if (available == 0)
				break;
			const size_t offset = data.size();
			data.resize(offset + available);
			DWORD read = 0;
			if (!WinHttpReadData(request, data.data() + offset, available, &read))
				break;
			data.resize(offset + read);
		}

		WinHttpCloseHandle(request);
		WinHttpCloseHandle(connect);
		WinHttpCloseHandle(session);

		if (data.empty())
			return std::nullopt;
		return data;
	}

	bool CopyIfExists(const fs::path& src, const fs::path& dst)
	{
		std::error_code ec;
		if (!fs::exists(src, ec))
			return false;
		fs::create_directories(dst.parent_path(), ec);
		fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
		return !ec;
	}

	std::vector<fs::path> LocalSourceCandidates(const fs::path& sourceDir, std::string_view relative)
	{
		std::vector<fs::path> candidates;
		candidates.push_back(sourceDir / fs::path(relative));

		// Legacy flat layout from older builds (bridge DLL + profiles at deploy root).
		if (relative == "bridge/meccha-xenos-bridge.dll")
		{
			candidates.push_back(sourceDir / L"meccha-xenos-bridge.dll");
			candidates.push_back(sourceDir / L"bridge" / L"meccha-xenos-bridge.dll");
		}
		else if (relative.rfind("bridge/mesh-profiles/", 0) == 0)
		{
			const std::string leaf = std::string(relative.substr(strlen("bridge/mesh-profiles/")));
			candidates.push_back(sourceDir / L"mesh-profiles" / fs::path(leaf));
			candidates.push_back(sourceDir / L"bridge" / L"mesh-profiles" / fs::path(leaf));
		}

		return candidates;
	}

	bool EnsureLocalFile(const fs::path& deployDir, const fs::path& sourceDir, std::string_view relative)
	{
		const fs::path dst = deployDir / fs::path(relative);
		if (fs::exists(dst))
			return true;

		for (const fs::path& src : LocalSourceCandidates(sourceDir, relative))
		{
			if (CopyIfExists(src, dst))
			{
				Log(L"[local] " + ToWide(relative));
				return true;
			}
		}

		LogError(L"Missing file: " + dst.wstring());
		return false;
	}

	bool DownloadFile(const Manifest& manifest, const fs::path& deployDir, std::string_view relative)
	{
		const fs::path dst = deployDir / fs::path(relative);
		const std::string url = JoinUrl(manifest.baseUrl, relative);
		Log(L"[download] " + ToWide(url));
		const auto bytes = HttpDownload(ToWide(url));
		if (!bytes)
		{
			LogError(L"Download failed: " + ToWide(relative));
			return false;
		}
		if (!WriteFileBytes(dst, *bytes))
		{
			LogError(L"Write failed: " + dst.wstring());
			return false;
		}
		return true;
	}

	bool VerifyBridgeLayout(const fs::path& deployDir)
	{
		const fs::path bridgeDll = deployDir / L"bridge" / L"meccha-xenos-bridge.dll";
		if (!fs::exists(bridgeDll))
		{
			LogError(L"Bridge DLL missing at: " + bridgeDll.wstring());
			return false;
		}

		const fs::path profilesDir = deployDir / L"bridge" / L"mesh-profiles";
		std::error_code ec;
		if (!fs::exists(profilesDir, ec) || !fs::is_directory(profilesDir, ec))
		{
			LogError(L"Bridge mesh-profiles folder missing at: " + profilesDir.wstring());
			return false;
		}

		Log(L"[ok] Bridge ready at: " + (deployDir / L"bridge").wstring());
		return true;
	}

	bool PrepareFiles(const Options& options, const Manifest& manifest)
	{
		std::error_code ec;
		fs::create_directories(options.deployDir, ec);
		fs::create_directories(options.deployDir / L"bridge" / L"mesh-profiles", ec);

		const bool useLocal = options.localOnly ||
			manifest.baseUrl.empty() ||
			manifest.baseUrl == "local";

		const fs::path sourceDir = ExeDirectory();
		for (const std::string& relative : manifest.files)
		{
			if (options.injectOnly)
			{
				const fs::path dst = options.deployDir / fs::path(relative);
				if (!fs::exists(dst))
				{
					LogError(L"Missing cached file (run without --inject-only): " + dst.wstring());
					return false;
				}
				continue;
			}

			if (useLocal)
			{
				if (!EnsureLocalFile(options.deployDir, sourceDir, relative))
					return false;
			}
			else if (!DownloadFile(manifest, options.deployDir, relative))
			{
				return false;
			}
		}

		return VerifyBridgeLayout(options.deployDir);
	}

	DWORD FindProcessId(const std::wstring& processName)
	{
		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return 0;

		DWORD pid = 0;
		if (Process32FirstW(snapshot, &entry))
		{
			do
			{
				if (_wcsicmp(entry.szExeFile, processName.c_str()) == 0)
				{
					pid = entry.th32ProcessID;
					break;
				}
			} while (Process32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
		return pid;
	}

	DWORD WaitForProcess(const std::wstring& processName, DWORD timeoutMs)
	{
		const ULONGLONG deadline = GetTickCount64() + timeoutMs;
		for (;;)
		{
			const DWORD pid = FindProcessId(processName);
			if (pid)
				return pid;
			if (GetTickCount64() >= deadline)
				return 0;
			Sleep(500);
		}
	}

	bool IsProcessRunning(DWORD pid)
	{
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		if (!process)
			return false;
		DWORD exitCode = 0;
		const bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
		CloseHandle(process);
		return alive;
	}

	bool InjectDll(DWORD pid, const fs::path& dllPath)
	{
		const std::wstring fullPath = fs::absolute(dllPath).wstring();
		if (!fs::exists(fullPath))
		{
			LogError(L"DLL not found: " + fullPath);
			return false;
		}

		HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
				PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
			FALSE, pid);
		if (!process)
		{
			LogError(L"OpenProcess failed (try running as admin). win32=" + std::to_wstring(GetLastError()));
			return false;
		}

		const SIZE_T bytes = (fullPath.size() + 1) * sizeof(wchar_t);
		LPVOID remote = VirtualAllocEx(process, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (!remote)
		{
			LogError(L"VirtualAllocEx failed. win32=" + std::to_wstring(GetLastError()));
			CloseHandle(process);
			return false;
		}

		if (!WriteProcessMemory(process, remote, fullPath.c_str(), bytes, nullptr))
		{
			LogError(L"WriteProcessMemory failed. win32=" + std::to_wstring(GetLastError()));
			VirtualFreeEx(process, remote, 0, MEM_RELEASE);
			CloseHandle(process);
			return false;
		}

		HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
		auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
		HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
		if (!thread)
		{
			LogError(L"CreateRemoteThread failed. win32=" + std::to_wstring(GetLastError()));
			VirtualFreeEx(process, remote, 0, MEM_RELEASE);
			CloseHandle(process);
			return false;
		}

		WaitForSingleObject(thread, 15000);
		DWORD remoteExit = 0;
		GetExitCodeThread(thread, &remoteExit);
		CloseHandle(thread);
		VirtualFreeEx(process, remote, 0, MEM_RELEASE);
		CloseHandle(process);

		if (remoteExit == 0)
		{
			LogError(L"LoadLibraryW returned null in the target process.");
			return false;
		}
		return true;
	}

	void PrintUsage()
	{
		std::wcout <<
			L"peterhack-loader\n"
			L"  Downloads required DLLs (or uses local copies) and injects peterhack.dll.\n\n"
			L"Usage:\n"
			L"  peterhack-loader.exe [options]\n\n"
			L"Options:\n"
			L"  --local         Use files next to the loader / deploy folder (no HTTP download)\n"
			L"  --inject-only   Skip download/copy; require files already in deploy folder\n"
			L"  --wait          Wait up to 5 minutes for the game process\n"
			L"  --manifest <path>  Manifest JSON (default: manifest.json next to loader)\n"
			L"  --deploy <dir>  Deploy folder (default: Desktop\\peterhack)\n"
			L"  --help          Show this help\n\n"
			L"Manifest baseUrl:\n"
			L"  \"local\"  -> copy from loader directory\n"
			L"  https://...  -> download each file listed in manifest.json\n"
			L"\nBridge files are deployed to <deploy>\\bridge\\ (where peterhack loads camo from).\n";
	}

	std::optional<Options> ParseArgs(int argc, wchar_t** argv)
	{
		Options options;
		options.deployDir = fs::path(DesktopDeployDir());
		options.manifestPath = fs::path(ExeDirectory()) / L"manifest.json";

		for (int i = 1; i < argc; ++i)
		{
			const std::wstring arg = argv[i];
		if (arg == L"--help" || arg == L"-h")
		{
			options.showHelp = true;
			continue;
		}
			if (arg == L"--local")
			{
				options.localOnly = true;
				continue;
			}
			if (arg == L"--inject-only")
			{
				options.injectOnly = true;
				continue;
			}
			if (arg == L"--wait")
			{
				options.waitForProcess = true;
				continue;
			}
			if (arg == L"--manifest" && i + 1 < argc)
			{
				options.manifestPath = fs::path(argv[++i]);
				continue;
			}
			if (arg == L"--deploy" && i + 1 < argc)
			{
				options.deployDir = fs::path(argv[++i]);
				continue;
			}
			LogError(L"Unknown argument: " + arg);
			PrintUsage();
			return std::nullopt;
		}
		return options;
	}
}

int wmain(int argc, wchar_t** argv)
{
	const auto options = ParseArgs(argc, argv);
	if (!options)
		return 1;
	if (options->showHelp)
	{
		PrintUsage();
		return 0;
	}

	const auto manifest = LoadManifest(options->manifestPath);
	if (!manifest)
	{
		const std::wstring manifestLabel = options->manifestPath.wstring();
		LogError(std::wstring(L"Failed to read manifest: ") + manifestLabel);
		return 2;
	}

	Log(std::wstring(L"Deploy folder: ") + options->deployDir.wstring());
	if (!options->injectOnly)
	{
		Log(L"Preparing files...");
		if (!PrepareFiles(*options, *manifest))
			return 3;
		Log(L"Files ready.");
	}
	else if (!VerifyBridgeLayout(options->deployDir))
	{
		return 3;
	}

	const std::wstring processName = !manifest->processName.empty() ? manifest->processName : options->processName;
	const std::wstring injectDll = !manifest->injectDll.empty() ? manifest->injectDll : options->injectDll;
	const fs::path dllPath = options->deployDir / injectDll;

	DWORD pid = FindProcessId(processName);
	if (!pid && options->waitForProcess)
	{
		Log(L"Waiting for " + processName + L"...");
		pid = WaitForProcess(processName, 5 * 60 * 1000);
	}
	if (!pid)
	{
		LogError(L"Process not found: " + processName + L" (launch the game or use --wait)");
		return 4;
	}
	if (!IsProcessRunning(pid))
	{
		LogError(L"Process exited before injection.");
		return 4;
	}

	Log(L"Injecting " + dllPath.wstring() + L" into pid " + std::to_wstring(pid));
	if (!InjectDll(pid, dllPath))
		return 5;

	Log(L"Injection succeeded. Press INSERT in-game to open the menu.");
	return 0;
}
