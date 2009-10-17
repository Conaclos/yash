# resolves dependencies of source files
# (C) 2009 magicant
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# This script takes directory names as the arguments.
# This script must be run in the root of the source tree.

if ! [ "$YASH_VERSION" ]; then
    echo This script must be run by yash.
    exit 2
fi
if [ $# -eq 0 ]; then
    echo $0: Arguments not specified.
    exit 2
fi

set -o nullglob
unset deptemp IFS
trap 'rm -rf "$deptemp"' EXIT
trap 'rm -rf "$deptemp"; exit 2' HUP INT QUIT ABRT TERM PIPE USR1 USR2
trap '' ALRM

maketempdir () {
    if [ x"${deptemp+set}" != x"set" ]; then
	mkdir "${deptemp=$PWD/.mkdeptmp}" || exit 2
	(cd "$deptemp" && mkdir -p "$@") || exit 2
    fi
}
filter () {
    sed -e '
	/^[[:blank:]]*#[[:blank:]]*include[[:blank:]]*"[^"]*"$/ {
	    s/^[[:blank:]]*#[[:blank:]]*include[[:blank:]]*"\([^"]*\)"/\1/
	    p
	}
	d
    ' "$1"
}
scan () {
    if ! [ -r "$1" ] || [ -r "$deptemp/$1" ]; then return 0; fi

    typeset dirname="$(dirname "$1")"
    while read -r dep; do
	printf '%s\n' "$dep"
	printresult "$dep" "$dirname"
    done <(filter "$1") >"$deptemp/$1"
}
printresult () {
    scan "$2/$1"
    if [ -s "$deptemp/$2/$1" ]; then
	prefix="$(dirname "$1")/"
	if [ "$prefix" = "./" ]; then prefix=""; fi
	while read -r dep; do
	    printf '%s%s\n' "$prefix" "$dep"
	done <"$deptemp/$2/$1"
    fi
}

template="#=#=# dependencies resolved by makedepend #=#=#"
for dir do
    maketempdir "$@"
    printf 'Resolving dependencies of source files in "%s"... ' "$dir"
    {
	printf '%s\n' "$template"
	for src in "$dir"/*.c; do
	    src="${src##*/}"
	    printresult "$src" "$dir" | sort -u | xargs echo "${src%.c}.o:"
	done
    } >"$dir/Makefile.deps"
    echo done
done


# vim: set ft=sh ts=8 sts=4 sw=4 noet tw=80: