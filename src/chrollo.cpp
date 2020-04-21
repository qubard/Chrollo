#include <iostream>
#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <direct.h>
#include <unordered_map>
#include <regex>
// We don't need detours--that's for plebs
#include "replace.h"

// A list of regex blacklist patterns
std::vector<std::regex> blacklist;

typedef char byte;

void GetModuleBaseAddress(DWORD procId, const wchar_t* modName, uintptr_t* modBaseAddr, uintptr_t* modSize) {
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
	if (hSnap != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 modEntry;
		modEntry.dwSize = sizeof(modEntry);
		if (Module32First(hSnap, &modEntry)) {
			do {
				if (!_wcsicmp(modEntry.szModule, modName)) {
					*modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
					*modSize = (uintptr_t)modEntry.modBaseSize;
					break;
				}
			} while (Module32Next(hSnap, &modEntry));
		}
	}
	CloseHandle(hSnap);
}

uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
	uintptr_t modBaseAddr = 0;
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
	if (hSnap != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 modEntry;
		modEntry.dwSize = sizeof(modEntry);
		if (Module32First(hSnap, &modEntry)) {
			do {
				if (!_wcsicmp(modEntry.szModule, modName)) {
					modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
					break;
				}
			} while (Module32Next(hSnap, &modEntry));
		}
	}
	CloseHandle(hSnap);
	return modBaseAddr;
}

void show_message(std::string str) {
	std::wstring wstr = std::wstring(str.begin(), str.end());
	MessageBox(NULL, wstr.c_str(), L"Chrollo", MB_OK | MB_SETFOREGROUND);
}

std::string read_string_from_ptr(char* loc) {
	int i = 0;
	std::string out;
	while (*((char*)(loc + i)) != '\0') {
		out += *((char*)(loc + i));
		i++;
	}
	return out;
}

void clear_string_ptr(char* loc, size_t size) {
	for (int i = 0; i < size; i++) {
		*((char*)(loc + i)) = 0;
	}
}

// Copy over data into the string at loc
void overwrite_string_ptr(char* loc, std::string data) {
	// It's enough to just copy over the string into loc and delimit it with a null terminator
	for (int i = 0; i < data.size(); i++) {
		*((char*)(loc + i)) = data[i];
	}
	loc[data.size()] = '\0';
}

std::vector<std::string> ignored_chars = { "!", "/", "<", ">", "|", "*", "?", ":", "\\", "\"" };
std::string last_server_name;

void __stdcall dump_script_buffer() {
	char* filename;
	char* buffer_ptr;
	__asm {
		mov buffer_ptr, esi
		mov filename, ecx
	}

	std::string script_content = read_string_from_ptr(buffer_ptr);
	std::string script_name = read_string_from_ptr(filename);
	std::string server_name = read_string_from_ptr((char*)(GetModuleBaseAddress(GetCurrentProcessId(), L"client.dll") + 0x6FF588));

	// Since we don't have an init server connection hook this will run once
	if (last_server_name != server_name) {
		server_name.erase(std::remove_if(server_name.begin(), server_name.end(),
			[](auto const& c) -> bool { return c == ':' || c =='?' || c == '|' || c == '"' || c == '\\' || c =='\/' || c == '<' || c == '>' || c == ' '; }), server_name.end());
		last_server_name = server_name;
	}

	// dump to disk
	std::fstream out;
	for (int i = 0; i < script_name.length(); i++) {
		// replace \ and / with _
		if ((int)script_name[i] == (int)'/' || (int)script_name[i] == (int)'\\') {
			script_name[i] = '_';
		}
	}

	// Replace script if found. 
	// Be careful, because replacing certain scripts can cause game crashes
	if (replace_table.find(script_name) != replace_table.end()) {
		overwrite_string_ptr(buffer_ptr, replace_table[script_name]);

		if (replace_table[script_name].length() > script_content.length()) {
			// Scripts cant be larger than what is allocated
			show_message("Replacement script " + script_name + " is too large");
		}

		show_message("Replaced script " + script_name);
	}
	else { // check the blacklist
		for (auto pattern : blacklist) {
			if (std::regex_search(script_name, pattern)) { // Matches if any substring matches the given pattern
				show_message("Blacklisted " + script_name + "!" + " with size " + std::to_string(strlen(buffer_ptr)));
				clear_string_ptr(buffer_ptr, script_content.size());
				break;
			}
		}
	}

	_mkdir(std::string("./chrollo/" + last_server_name).c_str());
	script_name = "./chrollo/" + last_server_name + "/" + script_name;
	out.open(script_name, std::fstream::out);
	out << script_content;
	if (out.fail()) {
		show_message("Failed to write to " + script_name);
	}
	out.close();
}

void hook_dump() {
	uintptr_t lua_shared_base, lua_shared_size;
	GetModuleBaseAddress(GetCurrentProcessId(), L"lua_shared.dll", &lua_shared_base, &lua_shared_size);
	uintptr_t luaJIT_version_2_0_4 = lua_shared_base + 0x5860;
	
	const int patchSize = 11;
	byte patch[patchSize] = { 0x8B, 0xCB, 0xff, 0xd0, 0x5f, 0x5e, 0x5b, 0x5d, 0xc2, 0x14, 0x00 };

	int hook_addr = -1;

	// `patch` is our signature as well
	for (byte* addr = (byte*)luaJIT_version_2_0_4; addr  < (byte*)luaJIT_version_2_0_4 + lua_shared_size - patchSize; addr++) {
		int m = 0;
		for (int i = 0; i < patchSize; i++) {
			if (*(addr + i) == (byte)patch[i]) {
				m++;
			}
			else {
				break;
			}
		}

		if (m == patchSize) {
			hook_addr = (int)addr;
			break;
		}
	}

	if (hook_addr < 0) {
		show_message("Chrollo failed to find valid hook! Terminating");
		return;
	}

	DWORD old;
	bool success = VirtualProtectEx(GetCurrentProcess(), (uintptr_t*)hook_addr, 20, PAGE_EXECUTE_READWRITE, &old);

	byte* patch_addr = (byte*)hook_addr;
	*((byte*)patch_addr) = 0x60; // PUSHAD
	patch_addr++;
	*((byte*)patch_addr) = 0xE8; // call
	patch_addr++;
	*((uintptr_t*)patch_addr) = (uintptr_t)&dump_script_buffer - (uintptr_t)patch_addr - 0x04;
	patch_addr += 4;

	*((byte*)patch_addr) = 0x61; // POPAD
	patch_addr++;

	// Patch back with original instructions
	for (int i = 0; i < patchSize; i++) {
		patch_addr[i] = patch[i];
	}

	VirtualProtectEx(GetCurrentProcess(), (uintptr_t*)hook_addr, 20, old, &old);
}


void read_blacklist(std::string root) {
	std::ifstream file;
	std::string dir = root + "/blacklist.txt";
	file.open(dir, std::ifstream::in);

	if (file.fail()) {
		show_message("Failed to read blacklist in " + dir);
	}
	else {
		std::string line;
		while (getline(file, line)) {
			blacklist.emplace_back(line);
		}
	}
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	std::string root_dir = "./chrollo";
	std::string replace_dir = std::string(root_dir + "/replace");
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		_mkdir(root_dir.c_str());
		_mkdir(replace_dir.c_str());
		read_blacklist(root_dir);
		read_replace_directory(replace_dir);
		hook_dump();
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

