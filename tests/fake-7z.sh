#!/bin/sh
# A stand-in for 7-Zip used by test_sevenzip: prints its arguments
# one per line to stdout and a marker to stderr.

if [ "$FAKE_7Z_MODE" = big ]; then
	# more than a pipe buffer on both outputs at the same time
	i=0
	while [ $i -lt 2000 ]; do
		echo "stdout line $i padded to make the output larger than a pipe buffer................"
		echo "stderr line $i padded to make the output larger than a pipe buffer................" >&2
		i=$((i + 1))
	done
	exit 0
fi

for arg in "$@"; do
	case "$arg" in
	@*)
		# print the list file instead of its random name
		printf 'list file:\n'
		cat "${arg#@}"
		;;
	*)
		printf '%s\n' "$arg"
		;;
	esac
done
echo "to stderr" >&2
exit 3
