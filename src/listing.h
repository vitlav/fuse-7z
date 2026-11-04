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
#pragma once

#include <cstdint>
#include <ctime>
#include <istream>
#include <string>
#include <vector>

#include <sys/types.h>

/**
 * One member of an archive as reported by `7z l -slt`.
 */
struct ArchiveEntry
{
    // name of the member exactly as 7-Zip reports it
    std::string path;
    bool is_dir = false;
    // file type from the Unix mode: '-', 'd', 'l', 'c', 'b', 'p', 's',
    // or 0 if the archive has no Unix mode for the member
    char type = 0;
    // permission bits, valid if type != 0
    mode_t mode = 0;
    uint64_t size = 0;
    bool has_mtime = false;
    time_t mtime = 0;
    long mtime_nsec = 0;
    std::string symlink;
    std::string hardlink;
    bool encrypted = false;
};

/**
 * Parse the output of `7z l -slt`. The block describing the archive
 * itself (before the "----------" line) is skipped.
 */
std::vector<ArchiveEntry> parse_7z_listing(std::istream & in);

/**
 * Parse a Unix mode string like "drwxr-xr-x".
 */
bool parse_mode_string(std::string const & text, char & type, mode_t & mode);

/**
 * Parse a 7-Zip time stamp like "2020-01-31 12:34:56.1234567"
 * (local time).
 */
bool parse_7z_time(std::string const & text, time_t & seconds, long & nsec);

/**
 * Split a member name into path components, dropping empty and "."
 * components.
 */
std::vector<std::string> split_archive_path(std::string const & path);
