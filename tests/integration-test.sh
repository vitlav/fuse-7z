#!/bin/bash
# Mount real archives with fuse-7z-ng and compare them with their
# source files.
#
# usage: integration-test.sh /path/to/fuse-7z-ng
#
# Exits with 77 (skipped) when FUSE or the 7-Zip tool is not available,
# for example in a build chroot or a container without /dev/fuse.

set -u

FUSE7Z=${1:?usage: $0 /path/to/fuse-7z-ng}
SKIP=77

skip()
{
	echo "SKIP: $*"
	exit $SKIP
}

fail()
{
	echo "FAIL: $*" >&2
	FAILED=1
}

[ -x "$FUSE7Z" ] || { echo "no executable $FUSE7Z" >&2; exit 1; }
[ -c /dev/fuse ] && [ -r /dev/fuse ] && [ -w /dev/fuse ] || skip "/dev/fuse is not usable"

FUSERMOUNT=
for tool in fusermount3 fusermount; do
	if command -v $tool >/dev/null 2>&1; then
		FUSERMOUNT=$tool
		break
	fi
done
[ -n "$FUSERMOUNT" ] || skip "fusermount3 is not found"

SEVENZIP=${FUSE_7Z_PROGRAM:-}
if [ -z "$SEVENZIP" ]; then
	for tool in 7zz 7z 7za; do
		if command -v $tool >/dev/null 2>&1; then
			SEVENZIP=$(command -v $tool)
			break
		fi
	done
fi
[ -n "$SEVENZIP" ] || skip "no 7-Zip command line tool (7zz, 7z, 7za) found"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/fuse-7z-it.XXXXXX") || exit 1
MNT=$WORK/mnt
mkdir "$MNT"
FAILED=0

is_mounted()
{
	# the mountpoint has another device than its parent once mounted
	[ "$(stat -c %d "$MNT" 2>/dev/null)" != "$(stat -c %d "$WORK")" ]
}

unmount()
{
	if is_mounted; then
		$FUSERMOUNT -u "$MNT" || $FUSERMOUNT -uz "$MNT"
	fi
}

cleanup()
{
	unmount
	rm -rf "$WORK"
}
trap cleanup EXIT

mount_archive()
{
	"$FUSE7Z" "$@" "$MNT" 2>"$WORK/stderr"
	local status=$?
	if [ $status -eq 0 ] && is_mounted; then
		return 0
	fi
	# FUSE is not permitted in this environment (container, chroot)
	if grep -qiE 'fuse: device not found|permission denied|operation not permitted|fusermount.*(not found|No such file)' "$WORK/stderr"; then
		skip "FUSE mount is not permitted: $(head -n1 "$WORK/stderr")"
	fi
	cat "$WORK/stderr" >&2
	return 1
}

# name, type, mode and size (of files) of everything below a directory
describe()
{
	(cd "$1" && find . -mindepth 1 \( -type d -printf '%P d %m\n' \) -o -printf '%P %y %m %s\n' | LC_ALL=C sort)
}

# file name and modification time in seconds
describe_mtime()
{
	(cd "$1" && find . -mindepth 1 -type f -printf '%P %TY-%Tm-%Td %TH:%TM:%.2TS\n' | LC_ALL=C sort)
}

# the source tree
SRC=$WORK/src
mkdir -p "$SRC/dir/nested/deeper" "$SRC/private" "$SRC/empty dir" "$SRC/ünïcødé дир"
echo "hello" > "$SRC/hello.txt"
: > "$SRC/empty"
printf 'nested\n' > "$SRC/dir/nested/file.txt"
printf 'deeper\n' > "$SRC/dir/nested/deeper/file.txt"
printf 'unicode\n' > "$SRC/ünïcødé дир/файл 日本.txt"
printf '#!/bin/sh\necho ok\n' > "$SRC/script.sh"
printf 'secret\n' > "$SRC/private/secret"
printf 'name with spaces\n' > "$SRC/dir/a file with spaces"
printf 'star\n' > "$SRC/dir/*"
printf 'dash\n' > "$SRC/dir/-rf"
# larger than a pipe buffer and any FUSE read request, not compressible
head -c 5000000 /dev/urandom > "$SRC/big.bin"
chmod 0755 "$SRC/script.sh"
chmod 0600 "$SRC/private/secret"
chmod 0700 "$SRC/private"
chmod 0640 "$SRC/hello.txt" "$SRC/empty"
touch -d '2001-02-03 04:05:06' "$SRC/hello.txt"

for type in 7z zip; do
	archive=$WORK/test.$type
	(cd "$SRC" && "$SEVENZIP" a -t$type -bd "$archive" . >/dev/null) || { fail "$type: cannot create the archive"; continue; }

	if ! mount_archive "$archive"; then
		fail "$type: mount failed"
		continue
	fi

	if diff -r "$SRC" "$MNT" >"$WORK/diff" 2>&1; then
		echo "PASS: $type: contents"
	else
		fail "$type: contents differ"
		cat "$WORK/diff" >&2
	fi

	if [ "$(describe "$SRC")" = "$(describe "$MNT")" ]; then
		echo "PASS: $type: names, types, modes and sizes"
	else
		fail "$type: names, types, modes or sizes differ"
		diff <(describe "$SRC") <(describe "$MNT") >&2
	fi

	if [ "$(describe_mtime "$SRC")" = "$(describe_mtime "$MNT")" ]; then
		echo "PASS: $type: modification times"
	else
		fail "$type: modification times differ"
		diff <(describe_mtime "$SRC") <(describe_mtime "$MNT") >&2
	fi

	# reading at an offset and past the end
	expected=$(tail -c +4999001 "$SRC/big.bin" | od -An -tx1 | tr -d ' \n')
	actual=$(dd if="$MNT/big.bin" bs=1000 skip=4999 2>/dev/null | od -An -tx1 | tr -d ' \n')
	[ "$expected" = "$actual" ] && echo "PASS: $type: read at offset" || fail "$type: read at offset"

	# two open handles of the same member
	exec 3<"$MNT/hello.txt" 4<"$MNT/hello.txt"
	if [ "$(cat <&3)" = hello ] && [ "$(cat <&4)" = hello ]; then
		echo "PASS: $type: concurrent handles"
	else
		fail "$type: concurrent handles"
	fi
	exec 3<&- 4<&-

	if "$MNT/script.sh" | grep -qx ok; then
		echo "PASS: $type: executable member"
	else
		fail "$type: executable member"
	fi

	if touch "$MNT/hello.txt" 2>/dev/null || echo x 2>/dev/null >"$MNT/new"; then
		fail "$type: the file system is writable"
	else
		echo "PASS: $type: read-only"
	fi

	unmount
	is_mounted && fail "$type: still mounted"
done

# an archive with members that point outside or are not regular files
if command -v tar >/dev/null 2>&1; then
	EVIL=$WORK/evil
	mkdir -p "$EVIL/inner" "$WORK/outside"
	echo good > "$EVIL/inner/good.txt"
	chmod 0644 "$EVIL/inner/good.txt"
	echo outside > "$WORK/outside/target.txt"
	ln -s /etc/passwd "$EVIL/inner/abs-link"
	ln -s ../../outside/target.txt "$EVIL/inner/rel-link"
	ln "$EVIL/inner/good.txt" "$EVIL/inner/hard-link"
	mkfifo "$EVIL/inner/fifo"
	(
		cd "$EVIL/inner" &&
		tar -cf "$WORK/evil.tar" good.txt abs-link rel-link hard-link fifo &&
		tar -rPf "$WORK/evil.tar" ../../outside/target.txt &&
		tar -rPf "$WORK/evil.tar" "$WORK/outside/target.txt"
	) || fail "evil: cannot create the archive"

	# p7zip 16.02 does not report the type of tar members, so it shows
	# the FIFO as an empty file
	expected="good.txt f 644 5"
	skipped=6
	if "$SEVENZIP" 2>&1 | head -n 3 | grep -q p7zip; then
		expected=$(printf 'fifo f 644 0\ngood.txt f 644 5')
		skipped=5
	fi
	if mount_archive "$WORK/evil.tar"; then
		listing=$(describe "$MNT")
		if [ "$listing" = "$expected" ]; then
			echo "PASS: evil: only the regular file is shown"
		else
			fail "evil: unexpected members"
			echo "$listing" >&2
		fi
		[ "$(cat "$MNT/good.txt")" = good ] || fail "evil: good.txt"
		if grep -q "skipped $skipped " "$WORK/stderr"; then
			echo "PASS: evil: skipped members are reported"
		else
			fail "evil: no report of the skipped members"
			cat "$WORK/stderr" >&2
		fi
		unmount
	else
		fail "evil: mount failed"
	fi
else
	echo "SKIP: evil: tar is not found"
fi

# not an archive
echo "not an archive" > "$WORK/broken.7z"
if "$FUSE7Z" "$WORK/broken.7z" "$MNT" 2>"$WORK/stderr"; then
	fail "broken: mounted"
	unmount
elif grep -q 'cannot list' "$WORK/stderr"; then
	echo "PASS: broken: refused"
else
	fail "broken: no error message"
	cat "$WORK/stderr" >&2
fi

# a missing archive
if "$FUSE7Z" "$WORK/missing.7z" "$MNT" 2>/dev/null; then
	fail "missing: mounted"
	unmount
else
	echo "PASS: missing: refused"
fi

exit $FAILED
