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

// Unit tests for the parser of `7z l -slt` output and the checks of
// member names. They need neither FUSE nor 7-Zip.

#include "listing.h"
#include "test.h"

#include <cstdlib>
#include <ctime>
#include <sstream>
#include <vector>

#include <sys/stat.h>

static std::vector<ArchiveEntry>
parse (std::string const & text)
{
    std::istringstream in(text);
    return parse_7z_listing(in);
}

// `7z l -slt` of a 7z archive (7-Zip 26.04)
static char const listing_7z[] =
    "\n"
    "7-Zip (z) 26.04 (x64) : Copyright (c) 1999-2026 Igor Pavlov : 2026-10-05\n"
    " 64-bit locale=C.UTF-8 Threads:4 OPEN_MAX:1024\n"
    "\n"
    "Scanning the drive for archives:\n"
    "1 file, 229 bytes (1 KiB)\n"
    "\n"
    "Listing archive: t.7z\n"
    "\n"
    "--\n"
    "Path = t.7z\n"
    "Type = 7z\n"
    "Physical Size = 229\n"
    "Headers Size = 211\n"
    "Method = LZMA2:12\n"
    "Solid = +\n"
    "Blocks = 1\n"
    "\n"
    "----------\n"
    "Path = dir\n"
    "Size = 0\n"
    "Packed Size = 0\n"
    "Modified = 2026-11-04 19:41:44.6629621\n"
    "Attributes = D drwxrwxr-x\n"
    "CRC = \n"
    "Encrypted = -\n"
    "Method = \n"
    "Block = \n"
    "\n"
    "Path = empty\n"
    "Size = 0\n"
    "Packed Size = 0\n"
    "Modified = 2026-11-04 19:41:44.6656016\n"
    "Attributes = A -rw-rw-r--\n"
    "CRC = \n"
    "Encrypted = -\n"
    "Method = \n"
    "Block = \n"
    "\n"
    "Path = dir/sub/\xc3\xbc.txt\n"
    "Size = 2\n"
    "Packed Size = 18\n"
    "Modified = 2026-11-04 19:41:44.6656016\n"
    "Attributes = A -rwxr-x---\n"
    "CRC = 363A3020\n"
    "Encrypted = +\n"
    "Method = LZMA2:12 7zAES:19\n"
    "Block = 0\n"
    "\n";

// `7z l -slt` of a zip archive with a symbolic link
static char const listing_zip[] =
    "--\n"
    "Path = t.zip\n"
    "Type = zip\n"
    "Physical Size = 787\n"
    "\n"
    "----------\n"
    "Path = a.txt\n"
    "Folder = -\n"
    "Size = 6\n"
    "Packed Size = 6\n"
    "Modified = 2026-11-04 19:41:44.6656016\n"
    "Created = \n"
    "Accessed = \n"
    "Attributes =  -rwxr-xr-x\n"
    "Encrypted = -\n"
    "Comment = \n"
    "\n"
    "Path = link\n"
    "Folder = -\n"
    "Size = 5\n"
    "Packed Size = 5\n"
    "Modified = 2026-11-04 19:41:44.6709623\n"
    "Attributes =  lrwxrwxrwx\n"
    "Encrypted = -\n"
    "\n"
    "Path = windows\n"
    "Folder = +\n"
    "Size = 0\n"
    "Attributes = D\n"
    "\n";

// `7z l -slt` of a GNU tar archive, which has an empty symbolic and
// hard link property for every member
static char const listing_tar[] =
    "--\n"
    "Path = t.tar\n"
    "Type = tar\n"
    "\n"
    "----------\n"
    "Path = .\n"
    "Folder = +\n"
    "Size = 0\n"
    "Modified = 2026-11-04 19:41:44\n"
    "Mode = drwxrwxr-x\n"
    "Symbolic Link = \n"
    "Hard Link = \n"
    "\n"
    "Path = ./a.txt\n"
    "Folder = -\n"
    "Size = 6\n"
    "Modified = 2026-11-04 19:41:44\n"
    "Mode = -rwsr-x--T\n"
    "Symbolic Link = \n"
    "Hard Link = \n"
    "\n"
    "Path = ./link\n"
    "Folder = -\n"
    "Size = 0\n"
    "Mode = lrwxrwxrwx\n"
    "Symbolic Link = a.txt\n"
    "Hard Link = \n"
    "\n"
    "Path = ./hard\n"
    "Folder = -\n"
    "Size = 0\n"
    "Mode = -rw-r--r--\n"
    "Symbolic Link = \n"
    "Hard Link = ./a.txt\n"
    "\n"
    "Path = ./fifo\n"
    "Folder = -\n"
    "Size = 0\n"
    "Mode = prw-r--r--\n"
    "Symbolic Link = \n"
    "Hard Link = \n";

static void
test_parse_7z ()
{
    std::vector<ArchiveEntry> entries = parse(listing_7z);
    CHECK_EQ(entries.size(), 3u);
    if (entries.size() != 3) {
        return;
    }

    CHECK_EQ(entries[0].path, "dir");
    CHECK(entries[0].is_dir);
    CHECK_EQ(entries[0].type, 'd');
    CHECK_EQ(entries[0].mode, static_cast<mode_t>(0775));

    CHECK_EQ(entries[1].path, "empty");
    CHECK(!entries[1].is_dir);
    CHECK_EQ(entries[1].size, 0u);
    CHECK_EQ(entries[1].mode, static_cast<mode_t>(0664));

    CHECK_EQ(entries[2].path, "dir/sub/\xc3\xbc.txt");
    CHECK_EQ(entries[2].size, 2u);
    CHECK_EQ(entries[2].type, '-');
    CHECK_EQ(entries[2].mode, static_cast<mode_t>(0750));
    CHECK(entries[2].encrypted);
    CHECK(entries[2].has_mtime);
    CHECK_EQ(entries[2].mtime_nsec, 665601600L);
    for (ArchiveEntry const & entry : entries) {
        CHECK(is_supported_entry(entry));
    }
}

static void
test_parse_zip ()
{
    std::vector<ArchiveEntry> entries = parse(listing_zip);
    CHECK_EQ(entries.size(), 3u);
    if (entries.size() != 3) {
        return;
    }

    CHECK_EQ(entries[0].path, "a.txt");
    CHECK_EQ(entries[0].mode, static_cast<mode_t>(0755));
    CHECK(is_supported_entry(entries[0]));

    CHECK_EQ(entries[1].path, "link");
    CHECK_EQ(entries[1].type, 'l');
    CHECK(!is_supported_entry(entries[1]));

    // only Windows attributes, no Unix mode
    CHECK_EQ(entries[2].path, "windows");
    CHECK(entries[2].is_dir);
    CHECK_EQ(entries[2].type, 0);
    CHECK(is_supported_entry(entries[2]));
}

static void
test_parse_tar ()
{
    std::vector<ArchiveEntry> entries = parse(listing_tar);
    CHECK_EQ(entries.size(), 5u);
    if (entries.size() != 5) {
        return;
    }

    CHECK_EQ(entries[0].path, ".");
    CHECK(entries[0].is_dir);
    CHECK(is_supported_entry(entries[0]));

    // the empty link properties do not make a link
    CHECK_EQ(entries[1].path, "./a.txt");
    CHECK(entries[1].symlink.empty());
    CHECK(entries[1].hardlink.empty());
    CHECK_EQ(entries[1].mode, static_cast<mode_t>(S_ISUID | S_ISVTX | 0750));
    CHECK(is_supported_entry(entries[1]));
    CHECK(entries[1].has_mtime);
    CHECK_EQ(entries[1].mtime_nsec, 0L);

    CHECK_EQ(entries[2].symlink, "a.txt");
    CHECK(!is_supported_entry(entries[2]));
    CHECK_EQ(entries[3].hardlink, "./a.txt");
    CHECK(!is_supported_entry(entries[3]));
    CHECK_EQ(entries[4].type, 'p');
    CHECK(!is_supported_entry(entries[4]));
}

// `7z l -slt` of a GNU tar archive by p7zip 16.02, which has no
// file type in the mode
static char const listing_tar_p7zip[] =
    "----------\n"
    "Path = dir\n"
    "Folder = +\n"
    "Mode = 0rwxr-x---\n"
    "\n"
    "Path = dir/a\n"
    "Folder = -\n"
    "Size = 2\n"
    "Mode = 0rw-r-----\n"
    "Symbolic Link = \n"
    "Hard Link = \n";

static void
test_parse_tar_p7zip ()
{
    std::vector<ArchiveEntry> entries = parse(listing_tar_p7zip);
    CHECK_EQ(entries.size(), 2u);
    if (entries.size() != 2) {
        return;
    }
    CHECK(entries[0].is_dir);
    CHECK_EQ(entries[0].type, 'd');
    CHECK_EQ(entries[0].mode, static_cast<mode_t>(0750));
    CHECK(is_supported_entry(entries[0]));
    CHECK_EQ(entries[1].type, '-');
    CHECK_EQ(entries[1].mode, static_cast<mode_t>(0640));
    CHECK(is_supported_entry(entries[1]));
}

static void
test_parse_edge_cases ()
{
    // no separator line: nothing is listed, not even the archive
    CHECK(parse("Path = t.7z\nType = 7z\n").empty());
    CHECK(parse("").empty());

    // CRLF line ends, no empty line between members, no trailing newline
    std::vector<ArchiveEntry> entries =
        parse("----------\r\nPath = a\r\nSize = 1\r\nPath = b\r\nSize = 22");
    CHECK_EQ(entries.size(), 2u);
    if (entries.size() == 2) {
        CHECK_EQ(entries[0].path, "a");
        CHECK_EQ(entries[0].size, 1u);
        CHECK_EQ(entries[1].path, "b");
        CHECK_EQ(entries[1].size, 22u);
    }

    // spaces in names are kept, a member without a name is dropped
    entries = parse("----------\nPath =  lead and trail \n\nSize = 5\n\n");
    CHECK_EQ(entries.size(), 1u);
    if (entries.size() == 1) {
        CHECK_EQ(entries[0].path, " lead and trail ");
    }

    // "=" inside the name
    entries = parse("----------\nPath = a = b\n");
    CHECK_EQ(entries.size(), 1u);
    if (entries.size() == 1) {
        CHECK_EQ(entries[0].path, "a = b");
    }
}

static void
test_mode_string ()
{
    char type = 0;
    mode_t mode = 0;
    CHECK(parse_mode_string("-rw-r--r--", type, mode));
    CHECK_EQ(type, '-');
    CHECK_EQ(mode, static_cast<mode_t>(0644));
    CHECK(parse_mode_string("drwxr-sr-x", type, mode));
    CHECK_EQ(type, 'd');
    CHECK_EQ(mode, static_cast<mode_t>(S_ISGID | 0755));
    CHECK(parse_mode_string("-rwSr--r--", type, mode));
    CHECK_EQ(mode, static_cast<mode_t>(S_ISUID | 0644));
    CHECK(parse_mode_string("drwxrwxrwt", type, mode));
    CHECK_EQ(mode, static_cast<mode_t>(S_ISVTX | 0777));

    CHECK(!parse_mode_string("", type, mode));
    CHECK(!parse_mode_string("A", type, mode));
    CHECK(!parse_mode_string("-rw-r--r-", type, mode));
    CHECK(!parse_mode_string("xrw-r--r--", type, mode));
    CHECK(!parse_mode_string("-rw-r--r-x-", type, mode));
    CHECK(!parse_mode_string("-wr-r--r--", type, mode));
}

static void
test_time ()
{
    time_t seconds = 0;
    long nsec = -1;
    CHECK(parse_7z_time("2020-01-31 12:34:56", seconds, nsec));
    CHECK_EQ(static_cast<long long>(seconds), 1580474096LL);
    CHECK_EQ(nsec, 0L);
    CHECK(parse_7z_time("2020-01-31 12:34:56.1234567", seconds, nsec));
    CHECK_EQ(nsec, 123456700L);
    CHECK(parse_7z_time("2020-01-31 12:34:56.123456789123", seconds, nsec));
    CHECK_EQ(nsec, 123456789L);
    CHECK(!parse_7z_time("", seconds, nsec));
    CHECK(!parse_7z_time("yesterday", seconds, nsec));
}

static std::string
normalized (std::string const & path, bool & ok)
{
    std::vector<std::string> components;
    ok = normalize_archive_path(path, components);
    std::string result;
    for (std::string const & component : components) {
        result += "<" + component + ">";
    }
    return result;
}

static void
test_normalize_path ()
{
    bool ok = false;
    CHECK_EQ(normalized("a/b/c", ok), "<a><b><c>");
    CHECK(ok);
    CHECK_EQ(normalized("./a//b/./c/", ok), "<a><b><c>");
    CHECK(ok);
    CHECK_EQ(normalized("\xc3\xbc/ file with spaces ", ok), "<\xc3\xbc>< file with spaces >");
    CHECK(ok);
    CHECK_EQ(normalized("...", ok), "<...>");
    CHECK(ok);
    CHECK_EQ(normalized("a..b/..c", ok), "<a..b><..c>");
    CHECK(ok);
    CHECK_EQ(normalized("-rf/@list/*?", ok), "<-rf><@list><*?>");
    CHECK(ok);

    // the root of the archive
    CHECK_EQ(normalized(".", ok), "");
    CHECK(ok);
    CHECK_EQ(normalized("./", ok), "");
    CHECK(ok);

    // names that point outside
    for (char const * bad : { "/etc/passwd", "//etc", "..", "../a", "a/../../b",
                              "a/..", "./..", "a/b/../c", "" }) {
        normalized(bad, ok);
        if (ok) {
            std::cerr << "accepted unsafe path '" << bad << "'" << std::endl;
            ++test_failures;
        }
    }
}

int
main ()
{
    // 7-Zip prints local time
    setenv("TZ", "UTC", 1);
    tzset();

    RUN_TEST(test_parse_7z);
    RUN_TEST(test_parse_zip);
    RUN_TEST(test_parse_tar);
    RUN_TEST(test_parse_tar_p7zip);
    RUN_TEST(test_parse_edge_cases);
    RUN_TEST(test_mode_string);
    RUN_TEST(test_time);
    RUN_TEST(test_normalize_path);
    return test_failures ? 1 : 0;
}
