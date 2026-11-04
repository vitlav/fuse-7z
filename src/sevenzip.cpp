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
#include "sevenzip.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static bool
is_executable (std::string const & path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
        access(path.c_str(), X_OK) == 0;
}

static std::string
search_path (std::string const & name)
{
    if (name.empty()) {
        return std::string();
    }
    if (name.find('/') != std::string::npos) {
        return is_executable(name) ? name : std::string();
    }

    char const * env = getenv("PATH");
    std::string path = env ? env : "/usr/local/bin:/usr/bin:/bin";
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(':', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        std::string dir = path.substr(start, end - start);
        if (dir.empty()) {
            dir = ".";
        }
        std::string candidate = dir + "/" + name;
        if (is_executable(candidate)) {
            return candidate;
        }
        start = end + 1;
    }
    return std::string();
}

std::string
SevenZip::find_program ()
{
    char const * env = getenv("FUSE_7Z_PROGRAM");
    if (env && *env) {
        return search_path(env);
    }

    // 7zz is 7-Zip itself, 7z is either 7-Zip or p7zip,
    // 7za is the standalone version with fewer formats
    for (char const * name : { "7zz", "7z", "7za" }) {
        std::string found = search_path(name);
        if (!found.empty()) {
            return found;
        }
    }
    return std::string();
}

SevenZip::SevenZip () :
    m_program(find_program())
{
    if (m_program.empty()) {
        throw std::runtime_error("7-Zip command line tool (7zz, 7z or 7za) is not found in PATH");
    }
}

SevenZip::SevenZip (std::string const & program) :
    m_program(program)
{
}

int
SevenZip::list (std::string const & archive, std::string & output,
                std::string & errors) const
{
    // -p sets an empty password, so 7-Zip never waits for one on stdin
    return run({ "l", "-slt", "-p", "--", archive }, -1, &output, errors);
}

int
SevenZip::extract (std::string const & archive, std::string const & member,
                   int out_fd, std::string & errors) const
{
    // -spd: the member name is not a wildcard, -so: write it to stdout
    std::vector<std::string> args = { "x", "-so", "-spd", "-bd", "-y", "-p" };
    if (member.empty() || member[0] != '@') {
        args.insert(args.end(), { "--", archive, member });
        return run(args, out_fd, nullptr, errors);
    }

    // p7zip reads a list file from an argument starting with @ even
    // after "--", so pass such a name in a list file of our own
    if (member.find('\n') != std::string::npos) {
        throw std::runtime_error("cannot extract a member with @ and a new line in its name");
    }
    char const * tmpdir = getenv("TMPDIR");
    std::string list_file = std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/fuse-7z-list-XXXXXX";
    int fd = mkstemp(&list_file[0]);
    if (fd < 0) {
        throw std::runtime_error("cannot create a list file: " + std::string(strerror(errno)));
    }
    std::string line = member + "\n";
    bool written = ::write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size());
    ::close(fd);
    if (!written) {
        unlink(list_file.c_str());
        throw std::runtime_error("cannot write a list file");
    }

    // no "--" here, it would stop the list file parsing of 7-Zip,
    // so make sure the archive name does not look like a switch
    std::string archive_arg = archive;
    if (archive_arg.empty() || archive_arg[0] != '/') {
        archive_arg = "./" + archive_arg;
    }
    args.insert(args.end(), { "-scsUTF-8", archive_arg, "@" + list_file });
    try {
        int status = run(args, out_fd, nullptr, errors);
        unlink(list_file.c_str());
        return status;
    }
    catch (...) {
        unlink(list_file.c_str());
        throw;
    }
}

static void
close_fd (int & fd)
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

int
SevenZip::run (std::vector<std::string> const & args, int out_fd,
               std::string * output, std::string & errors) const
{
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(m_program.c_str()));
    for (std::string const & arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };
    if (pipe2(err_pipe, O_CLOEXEC) != 0 ||
            (out_fd < 0 && pipe2(out_pipe, O_CLOEXEC) != 0)) {
        int error = errno;
        close_fd(err_pipe[0]);
        close_fd(err_pipe[1]);
        throw std::runtime_error(std::string("pipe: ") + strerror(error));
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, out_fd >= 0 ? out_fd : out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);

    pid_t pid;
    int spawn_error = posix_spawn(&pid, m_program.c_str(), &actions, nullptr,
                                  argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close_fd(out_pipe[1]);
    close_fd(err_pipe[1]);
    if (spawn_error != 0) {
        close_fd(out_pipe[0]);
        close_fd(err_pipe[0]);
        throw std::runtime_error("cannot run " + m_program + ": " + strerror(spawn_error));
    }

    // read stdout and stderr together, so that neither pipe can fill up
    struct pollfd fds[2];
    fds[0].fd = out_pipe[0];
    fds[0].events = POLLIN;
    fds[1].fd = err_pipe[0];
    fds[1].events = POLLIN;
    char buffer[65536];
    while (fds[0].fd >= 0 || fds[1].fd >= 0) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || fds[i].revents == 0) {
                continue;
            }
            ssize_t count = ::read(fds[i].fd, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                close(fds[i].fd);
                fds[i].fd = -1;
                continue;
            }
            std::string & target = (i == 0 && output) ? *output : errors;
            target.append(buffer, static_cast<size_t>(count));
        }
    }
    close_fd(fds[0].fd);
    close_fd(fds[1].fd);

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error(std::string("waitpid: ") + strerror(errno));
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 128 + WTERMSIG(status);
}
