#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
bool forbidden(const std::string &line) {
    return line.find("jni.h") != std::string::npos ||
           line.find("contract/kernel_access.hpp") != std::string::npos ||
           line.find("boundary/") != std::string::npos ||
           line.find("device/") != std::string::npos;
}
}  // namespace

int main() {
    const std::filesystem::path root = "src/backend/cve_2026_43284/lib";
    int hits = 0;
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension();
        if (ext != ".hpp" && ext != ".cpp") continue;
        std::ifstream input(entry.path());
        std::string line;
        int line_no = 0;
        while (std::getline(input, line)) {
            ++line_no;
            if (forbidden(line)) {
                std::cerr << entry.path() << ":" << line_no << ": " << line << "\n";
                ++hits;
            }
        }
    }
    if (hits != 0) {
        std::cerr << "include firewall: " << hits << " hit(s)\n";
        return 1;
    }
    std::cout << "include firewall: clean\n";
    return 0;
}
