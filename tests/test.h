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

// A minimal test harness, so the tests have no dependencies.

#include <iostream>
#include <sstream>
#include <string>

// exit code that tells ctest the test was skipped
#define TEST_SKIPPED 77

static int test_failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK(" #expr ") failed" << std::endl; \
            ++test_failures; \
        } \
    } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        auto const a_ = (actual); \
        auto const e_ = (expected); \
        if (!(a_ == e_)) { \
            std::ostringstream s_; \
            s_ << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #actual ", " #expected ") failed: got '" \
               << a_ << "', expected '" << e_ << "'"; \
            std::cerr << s_.str() << std::endl; \
            ++test_failures; \
        } \
    } while (0)

#define RUN_TEST(name) \
    do { \
        int before_ = test_failures; \
        name(); \
        std::cout << (test_failures == before_ ? "PASS: " : "FAIL: ") << #name << std::endl; \
    } while (0)
