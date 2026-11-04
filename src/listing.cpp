/*
 * This file is part of fuse-7z-ng.
 *
 * fuse-7z-ng is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * fuse-7z-ng is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with fuse-7z-ng.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "listing.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <sys/stat.h>

bool
parse_mode_string (std::string const & text, char & type, mode_t & mode)
{
    // p7zip 16.02 prints "0" as the file type of tar members
    if (text.size() != 10 || std::strchr("-dlcbps0", text[0]) == nullptr) {
        return false;
    }

    static char const letters[] = "rwxrwxrwx";
    mode_t result = 0;
    for (int i = 0; i < 9; ++i) {
        char c = text[static_cast<size_t>(i) + 1];
        mode_t bit = static_cast<mode_t>(1u << (8 - i));
        if (c == letters[i]) {
            result |= bit;
        } else if (i % 3 == 2 && (c == 's' || c == 't')) {
            // setuid/setgid/sticky together with execute
            result |= bit;
        } else if (c != '-' && !(i % 3 == 2 && (c == 'S' || c == 'T'))) {
            return false;
        }
    }

    // special bits
    char user_x = text[3], group_x = text[6], other_x = text[9];
    if (user_x == 's' || user_x == 'S') {
        result |= S_ISUID;
    }
    if (group_x == 's' || group_x == 'S') {
        result |= S_ISGID;
    }
    if (other_x == 't' || other_x == 'T') {
        result |= S_ISVTX;
    }

    type = text[0];
    mode = result;
    return true;
}

bool
parse_7z_time (std::string const & text, time_t & seconds, long & nsec)
{
    struct tm tm;
    std::memset(&tm, 0, sizeof(tm));
    char fraction[16] = "";
    int count = std::sscanf(text.c_str(), "%4d-%2d-%2d %2d:%2d:%2d.%15[0-9]",
                            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                            &tm.tm_hour, &tm.tm_min, &tm.tm_sec, fraction);
    if (count < 6) {
        return false;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    time_t result = mktime(&tm);
    if (result == static_cast<time_t>(-1)) {
        return false;
    }

    // up to 9 digits of the fraction are nanoseconds
    long ns = 0;
    size_t digits = 0;
    for (; digits < 9 && fraction[digits]; ++digits) {
        ns = ns * 10 + (fraction[digits] - '0');
    }
    for (; digits < 9; ++digits) {
        ns *= 10;
    }

    seconds = result;
    nsec = ns;
    return true;
}

static void
apply_property (ArchiveEntry & entry, std::string const & key, std::string const & value)
{
    if (key == "Path") {
        entry.path = value;
    } else if (key == "Folder") {
        entry.is_dir = (value == "+");
    } else if (key == "Size") {
        entry.size = std::strtoull(value.c_str(), nullptr, 10);
    } else if (key == "Modified") {
        entry.has_mtime = parse_7z_time(value, entry.mtime, entry.mtime_nsec);
    } else if (key == "Attributes" || key == "Mode") {
        // "D drwxr-xr-x", "A -rw-r--r--", " lrwxrwxrwx", "DA" or "drwxr-xr-x"
        std::istringstream tokens(value);
        std::string token;
        while (tokens >> token) {
            char type;
            mode_t mode;
            if (parse_mode_string(token, type, mode)) {
                entry.type = type;
                entry.mode = mode;
                if (type == 'd') {
                    entry.is_dir = true;
                }
            } else if (key == "Attributes" && token.find('D') != std::string::npos &&
                    token.find_first_not_of("RHSDAVNTPLCOIEUX") == std::string::npos) {
                // Windows attribute letters, D is FILE_ATTRIBUTE_DIRECTORY
                entry.is_dir = true;
            }
        }
    } else if (key == "Symbolic Link") {
        entry.symlink = value;
    } else if (key == "Hard Link") {
        entry.hardlink = value;
    } else if (key == "Encrypted") {
        entry.encrypted = (value == "+");
    }
}

static void
add_entry (std::vector<ArchiveEntry> & entries, ArchiveEntry & entry)
{
    if (entry.path.empty()) {
        return;
    }
    if (entry.type == '0') {
        // unknown file type: trust the folder flag
        entry.type = entry.is_dir ? 'd' : '-';
    }
    entries.push_back(entry);
}

std::vector<ArchiveEntry>
parse_7z_listing (std::istream & in)
{
    std::vector<ArchiveEntry> entries;
    std::string line;

    // skip the header and the properties of the archive itself
    bool found = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "----------") {
            found = true;
            break;
        }
    }
    if (!found) {
        return entries;
    }

    ArchiveEntry entry;
    bool has_properties = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            if (has_properties) {
                add_entry(entries, entry);
            }
            entry = ArchiveEntry();
            has_properties = false;
            continue;
        }

        size_t separator = line.find(" =");
        if (separator == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, separator);
        std::string value = line.substr(separator + 2);
        if (!value.empty() && value[0] == ' ') {
            value.erase(0, 1);
        }
        if (key == "Path" && has_properties) {
            // no empty line between two members
            add_entry(entries, entry);
            entry = ArchiveEntry();
        }
        apply_property(entry, key, value);
        has_properties = true;
    }
    if (has_properties) {
        add_entry(entries, entry);
    }
    return entries;
}

bool
normalize_archive_path (std::string const & path, std::vector<std::string> & components)
{
    components.clear();
    if (path.empty() || path[0] == '/') {
        return false;
    }

    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        std::string component = path.substr(start, end - start);
        if (component == "..") {
            components.clear();
            return false;
        }
        if (!component.empty() && component != ".") {
            components.push_back(component);
        }
        start = end + 1;
    }
    return true;
}

bool
is_supported_entry (ArchiveEntry const & entry)
{
    if (!entry.symlink.empty() || !entry.hardlink.empty()) {
        return false;
    }
    if (entry.type == 0) {
        return true;
    }
    return entry.type == (entry.is_dir ? 'd' : '-');
}
