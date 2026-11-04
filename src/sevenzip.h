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

#include <string>
#include <vector>

/**
 * Runs the 7-Zip command line tool (7zz, 7z or 7za).
 *
 * The tool is started directly with posix_spawn(), without a shell,
 * so archive and member names are never interpreted.
 */
class SevenZip
{
    public:
        /**
         * Use the program from the FUSE_7Z_PROGRAM environment variable,
         * or the first of 7zz, 7z, 7za found in PATH.
         * Throws std::runtime_error when nothing is found.
         */
        SevenZip();
        explicit SevenZip(std::string const & program);

        /**
         * Search for a 7-Zip executable, return an empty string if
         * there is none.
         */
        static std::string find_program();

        std::string const & program() const { return m_program; }

        /**
         * Write the technical listing (7z l -slt) of the archive
         * to output. Returns the exit code of the tool.
         */
        int list(std::string const & archive, std::string & output,
                 std::string & errors) const;

        /**
         * Extract a single member of the archive to the file descriptor.
         * Returns the exit code of the tool.
         */
        int extract(std::string const & archive, std::string const & member,
                    int out_fd, std::string & errors) const;

    private:
        int run(std::vector<std::string> const & args, int out_fd,
                std::string * output, std::string & errors) const;

        std::string m_program;
};
