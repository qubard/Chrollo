// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <iostream>
#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <sstream>
#include <fstream>
#include <direct.h>
// We don't need detours--that's for plebs

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

int index = 0;
void __stdcall dump_script_buffer() {
	int root_ptr;
	__asm {
		mov root_ptr, esi
	}

	int buffer_ptr = root_ptr + 0x34;
	int filename = root_ptr + 0x04;

	if (0xf < *(int*)(root_ptr + 0x48)) {
		buffer_ptr = *(int*)buffer_ptr;
	}

	// some weird filename sanitization check that gets done
	// cause sometimes filenames are invalid
	if (0xf < *(int*)(root_ptr + 0x18)) {
		filename = *(int*)filename;
	}
	char* filename2 = (char*)filename + 1;
	if (*(char*)filename != '!') {
		filename2 = (char*)filename;
	}

	std::string script_content = read_string_from_ptr((char*)buffer_ptr);
	std::string script_name = read_string_from_ptr(filename2);

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
	uintptr_t hook_addr = luaJIT_version_2_0_4 + 0x1c30;

	DWORD old;
	bool success = VirtualProtectEx(GetCurrentProcess(), (uintptr_t*)hook_addr, 15, PAGE_EXECUTE_READWRITE, &old);

	byte* patch_addr = (byte*)hook_addr;
	*((byte*)patch_addr) = 0x60; // PUSHAD
	patch_addr++;
	*((byte*)patch_addr) = 0xE8; // call
	patch_addr++;
	*((uintptr_t*)patch_addr) = (uintptr_t)&dump_script_buffer - (uintptr_t)patch_addr - 0x04; // 4 byte offset since it's relative to the start of this instruction
	patch_addr += 4;
	// RET 0x14
	patch_addr[0] = 0x61; // POPAD
	patch_addr[1] = 0xC2;
	patch_addr[2] = 0x14;
	patch_addr[3] = 0x00;

	VirtualProtectEx(GetCurrentProcess(), (uintptr_t*)hook_addr, 15, old, &old);
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		_mkdir("./script_stealer");
		hook_dump();
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

