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

// Lists and extracts real archives with the installed 7-Zip tool,
// without FUSE. Skipped when no 7-Zip tool is found.

#include "listing.h"
#include "sevenzip.h"
#include "test.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static std::string work_dir;

// member name -> contents
static std::map<std::string, std::string> const members = {
    { "plain.txt", "plain\n" },
    { "-rf", "looks like a switch\n" },
    { "@list", "looks like a list file\n" },
    { "*", "a star\n" },
    { "a?c", "a question mark\n" },
    { "abc", "matched by a?c as a wildcard\n" },
    { "space in name", "spaces\n" },
    { "quote'and\"dquote", "quotes\n" },
    { "$(touch pwned)", "command substitution\n" },
    { "\xc3\xbc-\xe6\x97\xa5\xe6\x9c\xac.txt", "unicode\n" },
    { "empty", "" },
    { "dir/nested/deep.txt", "deep\n" },
};

static int
run_in (std::string const & dir, std::vector<std::string> const & args)
{
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<char *> argv;
        for (std::string const & arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        if (chdir(dir.c_str()) != 0) {
            _exit(126);
        }
        int null = ::open("/dev/null", O_WRONLY);
        dup2(null, STDOUT_FILENO);
        execv(argv[0], argv.data());
        _exit(127);
    }
    int status = -1;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static std::string
temp_name (char const * suffix)
{
    char const * tmpdir = getenv("TMPDIR");
    return std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/fuse-7z-test-" + suffix;
}

static int
remove_entry (char const * path, struct stat const *, int, struct FTW *)
{
    return remove(path);
}

static std::string
read_fd (int fd)
{
    std::string result;
    char buffer[4096];
    ssize_t count;
    lseek(fd, 0, SEEK_SET);
    while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
        result.append(buffer, static_cast<size_t>(count));
    }
    return result;
}

static void
check_format (SevenZip const & sevenzip, std::string const & type)
{
    std::string archive = work_dir + "/test." + type;
    int status = run_in(work_dir + "/src",
                        { sevenzip.program(), "a", "-t" + type, "-bd", archive, "." });
    CHECK_EQ(status, 0);

    std::string output, errors;
    CHECK_EQ(sevenzip.list(archive, output, errors), 0);
    std::istringstream listing(output);
    std::vector<ArchiveEntry> entries = parse_7z_listing(listing);

    std::map<std::string, ArchiveEntry> files;
    for (ArchiveEntry const & entry : entries) {
        std::vector<std::string> components;
        CHECK(normalize_archive_path(entry.path, components));
        CHECK(is_supported_entry(entry));
        if (!entry.is_dir) {
            files[entry.path] = entry;
        }
    }
    CHECK_EQ(files.size(), members.size());

    for (auto const & member : members) {
        auto found = files.find(member.first);
        if (found == files.end()) {
            std::cerr << type << ": '" << member.first << "' is not listed" << std::endl;
            ++test_failures;
            continue;
        }
        CHECK_EQ(found->second.size, member.second.size());
        CHECK_EQ(found->second.mode & 0777, static_cast<mode_t>(0640));

        std::string name = temp_name("XXXXXX");
        int fd = mkstemp(&name[0]);
        CHECK(fd >= 0);
        unlink(name.c_str());
        std::string extract_errors;
        CHECK_EQ(sevenzip.extract(archive, member.first, fd, extract_errors), 0);
        std::string data = read_fd(fd);
        close(fd);
        if (data != member.second) {
            std::cerr << type << ": '" << member.first << "' extracted as '" << data << "'" << std::endl;
            ++test_failures;
        }
    }

    CHECK(access((work_dir + "/src/pwned").c_str(), F_OK) != 0);
    CHECK(access("pwned", F_OK) != 0);
}

static void
test_7z ()
{
    check_format(SevenZip(), "7z");
}

static void
test_zip ()
{
    check_format(SevenZip(), "zip");
}

static void
test_broken_archive ()
{
    std::string archive = work_dir + "/broken.7z";
    std::ofstream(archive) << "this is not an archive";
    SevenZip sevenzip;
    std::string output, errors;
    int status = sevenzip.list(archive, output, errors);
    CHECK(status != 0);
    std::istringstream listing(output);
    CHECK(parse_7z_listing(listing).empty());
}

int
main ()
{
    if (SevenZip::find_program().empty()) {
        std::cout << "SKIP: no 7-Zip command line tool (7zz, 7z, 7za) found" << std::endl;
        return TEST_SKIPPED;
    }

    std::string dir = temp_name("XXXXXX");
    if (!mkdtemp(&dir[0])) {
        perror("mkdtemp");
        return 1;
    }
    work_dir = dir;
    umask(022);
    for (auto const & member : members) {
        std::string path = work_dir + "/src/" + member.first;
        std::string parent = path.substr(0, path.rfind('/'));
        for (size_t slash = work_dir.size() + 1; slash != std::string::npos;
                slash = parent.find('/', slash + 1)) {
            mkdir(parent.substr(0, slash).c_str(), 0755);
        }
        mkdir(parent.c_str(), 0755);
        std::ofstream(path, std::ios::binary) << member.second;
        chmod(path.c_str(), 0640);
    }

    RUN_TEST(test_7z);
    RUN_TEST(test_zip);
    RUN_TEST(test_broken_archive);

    nftw(work_dir.c_str(), remove_entry, 16, FTW_DEPTH | FTW_PHYS);
    return test_failures ? 1 : 0;
}
