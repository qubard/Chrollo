// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <iostream>
#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <sstream>
#include <fstream>
#include <direct.h>
#include <unordered_map>
#include <set>
// We don't need detours--that's for plebs
#include "replace.h"

std::set<std::string> blacklist;

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
	MessageBox(NULL, wstr.c_str(), L"", MB_OK | MB_SETFOREGROUND);
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

// copy over data into the string at loc
void overwrite_string_ptr(char* loc, std::string data) {
	// It's enough to just copy over the string into loc and delimit it with a null terminator
	for (int i = 0; i < data.size(); i++) {
		*((char*)(loc + i)) = data[i];
	}
	loc[data.size()] = '\0';
}

void __stdcall dump_script_buffer() {
	char* filename;
	char* buffer_ptr;
	__asm {
		mov buffer_ptr, esi
		mov filename, ecx
	}

	std::string script_content = read_string_from_ptr(buffer_ptr);
	std::string script_name = read_string_from_ptr(filename);

	if (script_content.find("screenshot_internal") != std::string::npos) {
		show_message("Found screenshot_internal! " + script_name);
	}

	// dump to disk
	std::fstream out;
	for (int i = 0; i < script_name.length(); i++) {
		if ((int)script_name[i] == 47) {
			script_name[i] = '_';
		}
	}

	if (blacklist.find(script_name) != blacklist.end()) {
		show_message("Blacklisted " + script_name + "!" + " with size " + std::to_string(strlen(buffer_ptr)));
		clear_string_ptr(buffer_ptr, script_content.size()); // surprised this region is writable lol
	} else if (replace_table.find(script_name) != replace_table.end()) {
		overwrite_string_ptr(buffer_ptr, replace_table[script_name]);
	}

	script_name = "./script_stealer/" + script_name;
	out.open(script_name, std::fstream::out);
	out << script_content;
	if (out.fail()) {
		show_message("Failed to write to " + script_name);
	}
	out.close();
}

typedef char byte;

void hook_dump() {
	uintptr_t base = GetModuleBaseAddress(GetCurrentProcessId(), L"lua_shared.dll");
	uintptr_t luaJIT_version_2_0_4 = base + 0x5860;
	uintptr_t hook_addr = luaJIT_version_2_0_4 + 0x1C28;

	DWORD old;
	bool success = VirtualProtectEx(GetCurrentProcess(), (uintptr_t*)hook_addr, 20, PAGE_EXECUTE_READWRITE, &old);

	byte* patch_addr = (byte*)hook_addr;
	*((byte*)patch_addr) = 0x60; // PUSHAD
	patch_addr++;
	*((byte*)patch_addr) = 0xE8; // call
	patch_addr++;
	*((uintptr_t*)patch_addr) = (uintptr_t)&dump_script_buffer - (uintptr_t)patch_addr - 0x04;
	patch_addr += 4;

	// POPAD then patch back with original instructions
	byte patch[12] = { 0x61, 0x8B, 0xCB, 0xff, 0xd0, 0x5f, 0x5e, 0x5b, 0x5d, 0xc2, 0x14, 0x00 };
	for (int i = 0; i < 12; i++) {
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

	std::string line;
	while (getline(file, line)) {
		blacklist.emplace(line.c_str());
	}
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	std::string root_dir = "./script_stealer";
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