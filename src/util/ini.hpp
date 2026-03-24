#pragma once

#include <cstdio>
#include <cstring>

namespace er::util {

// Minimal INI parser. Reads lines from an open FILE*, calls handler(user, section, name, value)
// for each key-value pair. Supports [section] headers, ';' and '#' comments, '=' and ':' separators.
// Handler returning 0 stops parsing early. Returns 0 on success.
inline int parseIniFile(FILE *file, int (*handler)(void *user, const char *section, const char *name, const char *value), void *user) {
    char line[512];
    char section[256] = "";

    while (std::fgets(line, sizeof(line), file)) {
        // Strip leading whitespace
        char *start = line;
        while (*start == ' ' || *start == '\t') ++start;

        // Strip trailing whitespace / newline
        char *end = start + std::strlen(start);
        while (end > start && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) --end;
        *end = '\0';

        // Skip blank lines and comments
        if (*start == '\0' || *start == ';' || *start == '#') continue;

        // Section header
        if (*start == '[') {
            char *close = std::strchr(start + 1, ']');
            if (close) {
                *close = '\0';
                std::strncpy(section, start + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
            }
            continue;
        }

        // Key = value
        char *sep = std::strchr(start, '=');
        if (!sep) sep = std::strchr(start, ':');
        if (sep) {
            // Trim key (right side)
            char *keyEnd = sep;
            while (keyEnd > start && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) --keyEnd;
            *keyEnd = '\0';

            // Trim value (left side)
            char *val = sep + 1;
            while (*val == ' ' || *val == '\t') ++val;

            if (handler(user, section, start, val) == 0) break;
        }
    }
    return 0;
}

} // namespace er::util
