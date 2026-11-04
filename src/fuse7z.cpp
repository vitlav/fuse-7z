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
#include "fuse7z.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include <sys/stat.h>
#include <unistd.h>

ExtractedFile::~ExtractedFile()
{
    if (fd >= 0) {
        ::close(fd);
    }
}

static std::string
absolute_path (std::string const & filename, std::string const & cwd)
{
    if (!filename.empty() && filename[0] == '/') {
        return filename;
    }
    return cwd + "/" + filename;
}

static std::string
first_line (std::string const & text)
{
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find_first_not_of(" \t\r") != std::string::npos) {
            return line;
        }
    }
    return std::string();
}

Fuse7z::Fuse7z(std::string const & filename, std::string const & cwd) :
         archive_fn (absolute_path(filename, cwd)),
         cwd (cwd)
{
    root_node = new Node(nullptr, "");
    root_node->is_dir = true;

    Logger &logger = Logger::instance ();
    logger << "Initialization of fuse-7z with archive " << archive_fn << " using " << sevenzip.program() << Logger::endl;

    if (stat(archive_fn.c_str(), &archive_stat) != 0) {
        delete root_node;
        throw std::runtime_error("cannot open " + archive_fn + ": " + strerror(errno));
    }
    root_node->stat.st_mtim = archive_stat.st_mtim;
    root_node->stat.st_atim = archive_stat.st_atim;
    root_node->stat.st_ctim = archive_stat.st_ctim;

    std::string output, errors;
    int status = sevenzip.list(archive_fn, output, errors);
    // 1 is a warning, the listing is still usable
    if (status != 0 && status != 1) {
        delete root_node;
        std::string reason = first_line(errors);
        throw std::runtime_error("cannot list " + archive_fn +
                                 (reason.empty() ? std::string() : ": " + reason));
    }

    std::istringstream listing(output);
    std::vector<ArchiveEntry> entries = parse_7z_listing(listing);
    logger << "Archive contains " << entries.size() << " entries" << Logger::endl;
    size_t skipped = 0;
    for (ArchiveEntry const & entry : entries) {
        if (!add_entry(entry)) {
            ++skipped;
        }
    }
    if (skipped) {
        Logger::instance().err("fuse-7z-ng: skipped " + std::to_string(skipped) +
                               " unsafe or unsupported archive entries");
    }
}

bool Fuse7z::add_entry(ArchiveEntry const & entry) {
    Logger &logger = Logger::instance ();
    std::vector<std::string> components;
    if (!normalize_archive_path(entry.path, components)) {
        logger << "Skipping entry with unsafe path " << entry.path << Logger::endl;
        return false;
    }
    if (components.empty()) {
        // the root directory itself, as stored by tar
        return true;
    }
    if (!is_supported_entry(entry)) {
        logger << "Skipping link or special file " << entry.path << Logger::endl;
        return false;
    }

    Node * node = root_node;
    for (size_t i = 0; i < components.size(); ++i) {
        nodelist_t::iterator found = node->childs.find(components[i].c_str());
        if (found != node->childs.end()) {
            node = found->second;
            bool last = (i + 1 == components.size());
            if (!last && !node->is_dir) {
                logger << "Skipping " << entry.path << ": a parent is a file" << Logger::endl;
                return false;
            }
            if (last && (!node->is_dir || !entry.is_dir)) {
                // the same file twice, or a file and a directory with the same name
                logger << "Skipping duplicate entry " << entry.path << Logger::endl;
                return false;
            }
            continue;
        }
        Node * child = new Node(node, components[i].c_str());
        node->childs[child->name] = child;
        // implied parent directories get the time of the archive
        child->is_dir = true;
        child->stat.st_mtim = archive_stat.st_mtim;
        child->stat.st_atim = archive_stat.st_atim;
        child->stat.st_ctim = archive_stat.st_ctim;
        node = child;
    }

    node->is_dir = entry.is_dir;
    node->archive_path = entry.path;
    node->stat.st_size = entry.is_dir ? 0 : static_cast<off_t>(entry.size);
    if (entry.type) {
        node->stat.st_mode = (entry.is_dir ? S_IFDIR : S_IFREG) | entry.mode;
    }
    if (entry.has_mtime) {
        node->stat.st_mtim.tv_sec = entry.mtime;
        node->stat.st_mtim.tv_nsec = entry.mtime_nsec;
        node->stat.st_atim = node->stat.st_mtim;
        node->stat.st_ctim = node->stat.st_mtim;
    }
    return true;
}

Fuse7z::~Fuse7z() {
    delete root_node;
}

void Fuse7z::open(char const * path, Node * node) {
    Logger &logger = Logger::instance ();
    logger << "Opening file " << path << "(" << node->fullname() << ")" << Logger::endl;

    ExtractedFile * file = dynamic_cast<ExtractedFile *>(node->buffer);
    if (file) {
        ++file->refs;
        return;
    }

    char const * tmpdir = getenv("TMPDIR");
    std::string temp = std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/fuse-7z-XXXXXX";
    int fd = mkstemp(&temp[0]);
    if (fd < 0) {
        throw std::runtime_error("cannot create a temporary file in " + temp + ": " + strerror(errno));
    }
    // nobody else needs to see the extracted data
    unlink(temp.c_str());
    file = new ExtractedFile(fd);

    std::string errors;
    int status = sevenzip.extract(archive_fn, node->archive_path, fd, errors);
    struct stat st;
    if (status != 0 || fstat(fd, &st) != 0 || st.st_size != node->stat.st_size) {
        delete file;
        std::string reason = first_line(errors);
        throw std::runtime_error("cannot extract " + node->archive_path +
                                 (reason.empty() ? std::string() : ": " + reason));
    }
    node->buffer = file;
}

void Fuse7z::close(char const * path, Node * node) {
    Logger &logger = Logger::instance ();
    logger << "Closing file " << path << "(" << node->fullname() << ")" << Logger::endl;
    ExtractedFile * file = dynamic_cast<ExtractedFile *>(node->buffer);
    if (file && --file->refs == 0) {
        delete file;
        node->buffer = nullptr;
    }
}

int Fuse7z::read(char const * path, Node * node, char * buf, size_t size, off_t offset) {
    Logger &logger = Logger::instance ();
    logger << "Reading file " << path << "(" << node->fullname() << ") for " << size << " at " << offset << Logger::endl;
    ExtractedFile * file = dynamic_cast<ExtractedFile *>(node->buffer);
    if (!file) {
        return -EBADF;
    }

    size_t done = 0;
    while (done < size) {
        ssize_t count = pread(file->fd, buf + done, size - done, offset + static_cast<off_t>(done));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        if (count == 0) {
            break;
        }
        done += static_cast<size_t>(count);
    }
    return static_cast<int>(done);
}
