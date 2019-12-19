#pragma once

#include <filesystem>
#include "trim.h"

std::unordered_map<std::string, std::string> replace_table;

namespace fs = std::filesystem;

void read_replace_directory(std::string path) {
	replace_table.clear();
	for (const auto& entry : fs::directory_iterator(path)) {
		std::ifstream file;
		file.open(entry.path().string(), std::ifstream::in);

		if (!file.fail()) {
			std::string file_out;
			std::string line;
			while (getline(file, line)) {
				file_out += line + " ";
			}
			std::string filename = fs::path(entry.path()).filename().string();
			file_out = file_out.substr(0, file_out.size() - 1);
			replace_table[filename] = file_out;
		}
	}
}