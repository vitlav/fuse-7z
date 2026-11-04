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

// Tests of how the 7-Zip tool is found and started, with a fake tool
// that prints its arguments (FAKE_7Z is its path).

#include "sevenzip.h"
#include "test.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include <unistd.h>

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
test_find_program ()
{
    setenv("FUSE_7Z_PROGRAM", FAKE_7Z, 1);
    CHECK_EQ(SevenZip::find_program(), std::string(FAKE_7Z));
    CHECK_EQ(SevenZip().program(), std::string(FAKE_7Z));

    setenv("FUSE_7Z_PROGRAM", "/nonexistent/7zz", 1);
    CHECK_EQ(SevenZip::find_program(), std::string());
    bool thrown = false;
    try {
        SevenZip sevenzip;
    }
    catch (std::runtime_error &) {
        thrown = true;
    }
    CHECK(thrown);

    // looked up in PATH when there is no slash
    setenv("FUSE_7Z_PROGRAM", "sh", 1);
    CHECK(!SevenZip::find_program().empty());
    unsetenv("FUSE_7Z_PROGRAM");
}

static void
test_list_arguments ()
{
    SevenZip sevenzip(FAKE_7Z);
    std::string output, errors;
    // nothing in the name may be interpreted by a shell or by 7-Zip
    int status = sevenzip.list("-x $(touch pwned) 'q\" *.7z", output, errors);
    CHECK_EQ(status, 3);
    CHECK_EQ(output, "l\n-slt\n-p\n--\n-x $(touch pwned) 'q\" *.7z\n");
    CHECK_EQ(errors, "to stderr\n");
    CHECK(access("pwned", F_OK) != 0);
}

static void
test_extract_arguments ()
{
    SevenZip sevenzip(FAKE_7Z);
    FILE * file = tmpfile();
    CHECK(file != nullptr);
    if (!file) {
        return;
    }
    int fd = fileno(file);

    std::string errors;
    int status = sevenzip.extract("a.7z", "-r *", fd, errors);
    CHECK_EQ(status, 3);
    CHECK_EQ(read_fd(fd), "x\n-so\n-spd\n-bd\n-y\n-p\n--\na.7z\n-r *\n");
    CHECK_EQ(errors, "to stderr\n");
    fclose(file);
}

static void
test_extract_list_file ()
{
    SevenZip sevenzip(FAKE_7Z);
    FILE * file = tmpfile();
    CHECK(file != nullptr);
    if (!file) {
        return;
    }
    int fd = fileno(file);

    // a name starting with @ goes through a list file
    std::string errors;
    int status = sevenzip.extract("-a.7z", "@list -r *", fd, errors);
    CHECK_EQ(status, 3);
    CHECK_EQ(read_fd(fd), "x\n-so\n-spd\n-bd\n-y\n-p\n-scsUTF-8\n./-a.7z\nlist file:\n@list -r *\n");
    fclose(file);

    bool thrown = false;
    try {
        sevenzip.extract("a.7z", "@a\nb", -1, errors);
    }
    catch (std::runtime_error &) {
        thrown = true;
    }
    CHECK(thrown);
}

static void
test_large_output ()
{
    setenv("FAKE_7Z_MODE", "big", 1);
    SevenZip sevenzip(FAKE_7Z);
    std::string output, errors;
    CHECK_EQ(sevenzip.list("a.7z", output, errors), 0);
    CHECK(output.size() > 100000);
    CHECK(errors.size() > 100000);
    unsetenv("FAKE_7Z_MODE");
}

static void
test_missing_program ()
{
    SevenZip sevenzip("/nonexistent/7zz");
    std::string output, errors;
    bool thrown = false;
    try {
        sevenzip.list("a.7z", output, errors);
    }
    catch (std::runtime_error &) {
        thrown = true;
    }
    CHECK(thrown);
}

int
main ()
{
    RUN_TEST(test_find_program);
    RUN_TEST(test_list_arguments);
    RUN_TEST(test_extract_arguments);
    RUN_TEST(test_extract_list_file);
    RUN_TEST(test_large_output);
    RUN_TEST(test_missing_program);
    return test_failures ? 1 : 0;
}
