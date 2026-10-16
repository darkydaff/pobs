#pragma once

#include <string>
#include <vector>
#include <array>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>

namespace pobs {

struct ProcessResult {
    int exit_code = -1;
    std::string stdout_str;
    std::string stderr_str;
};

inline bool is_running_in_flatpak() {
    static bool checked = false;
    static bool in_flatpak = false;
    if (!checked) {
        checked = true;
        in_flatpak = (access("/.flatpak-info", F_OK) == 0) ||
                     (getenv("FLATPAK_ID") != nullptr) ||
                     (access("/usr/bin/flatpak-spawn", X_OK) == 0);
    }
    return in_flatpak;
}

inline ProcessResult run_process(const std::vector<std::string>& args, int timeout_ms = 5000) {
    (void)timeout_ms;
    ProcessResult res;
    if (args.empty()) {
        res.exit_code = -1;
        res.stderr_str = "Empty argument list";
        return res;
    }

    std::string cmd;
    if (is_running_in_flatpak() && args[0] == "adb") {
        cmd = "/usr/bin/flatpak-spawn --host ";
    }

    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0 || !cmd.empty()) {
            if (!cmd.empty() && cmd.back() != ' ') {
                cmd += " ";
            }
        }
        const std::string& arg = args[i];
        bool needs_quotes = false;
        for (char c : arg) {
            if (c == ' ' || c == '"' || c == '\'' || c == ';' || c == '&' || c == '|' || c == '$' || c == '\\') {
                needs_quotes = true;
                break;
            }
        }
        if (needs_quotes) {
            cmd += "\"";
            for (char c : arg) {
                if (c == '"' || c == '\\' || c == '$') cmd += '\\';
                cmd += c;
            }
            cmd += "\"";
        } else {
            cmd += arg;
        }
    }

    cmd += " 2>&1";

    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        res.exit_code = -1;
        res.stderr_str = "popen() failed";
        return res;
    }

    std::array<char, 512> buffer;
    while (fgets(buffer.data(), buffer.size(), fp) != nullptr) {
        res.stdout_str += buffer.data();
    }

    int status = pclose(fp);
    if (WIFEXITED(status)) {
        res.exit_code = WEXITSTATUS(status);
    } else {
        res.exit_code = status;
    }

    return res;
}

} // namespace pobs
