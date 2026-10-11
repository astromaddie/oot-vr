#!/usr/bin/env bash
# Reconstruct the pinned Steam Frame libultraship source from its public base plus
# the tracked patch. The resulting worktree tree must match the former private pin.
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
lib="${repo}/libultraship"
patch_file="${repo}/patches/libultraship-steam-frame.patch"
base_commit="c59bbc35edfb50670ce841f13008c475bb05041e"
base_tree="392bf6c7f647b7c6a5c51cc1f900a71404c1410a"
target_tree="fc24a05187dcd1e90f598aa97b456c8b7ffb2863"
patch_sha256="f7e92c135e23b0e8d6b90d8f10c06be440b3687568be01afc3960cd4078e959a"

if [[ ! -d "${lib}/.git" && ! -f "${lib}/.git" ]]; then
    echo "libultraship is not initialized; run git submodule update --init libultraship" >&2
    exit 2
fi
if [[ "$(git -C "$lib" rev-parse --verify HEAD)" != "$base_commit" ]]; then
    echo "libultraship must be checked out at public base ${base_commit}" >&2
    exit 2
fi
if [[ "$(git -C "$lib" rev-parse HEAD^{tree})" != "$base_tree" ]]; then
    echo "libultraship public base tree does not match the pinned tree" >&2
    exit 2
fi

if command -v shasum >/dev/null 2>&1; then
    actual_patch_sha="$(shasum -a 256 "$patch_file" | awk '{print $1}')"
elif command -v sha256sum >/dev/null 2>&1; then
    actual_patch_sha="$(sha256sum "$patch_file" | awk '{print $1}')"
else
    echo "shasum or sha256sum is required to verify the source patch" >&2
    exit 2
fi
if [[ "$actual_patch_sha" != "$patch_sha256" ]]; then
    echo "libultraship source patch SHA-256 mismatch" >&2
    exit 2
fi

worktree_tree() {
    local index_dir tree
    index_dir="$(mktemp -d "${TMPDIR:-/tmp}/oot-libultraship-index.XXXXXX")"
    GIT_INDEX_FILE="${index_dir}/index" git -C "$lib" read-tree HEAD
    GIT_INDEX_FILE="${index_dir}/index" git -C "$lib" add -A
    tree="$(GIT_INDEX_FILE="${index_dir}/index" git -C "$lib" write-tree)"
    rm -rf "$index_dir"
    printf '%s\n' "$tree"
}

current_tree="$(worktree_tree)"
if [[ "$current_tree" == "$target_tree" ]]; then
    echo "libultraship Steam Frame source is prepared (${target_tree})"
    exit 0
fi
if [[ "$current_tree" != "$base_tree" ]]; then
    echo "libultraship has local changes outside the pinned Steam Frame patch" >&2
    exit 2
fi

git -C "$lib" apply --check --whitespace=nowarn "$patch_file"
git -C "$lib" apply --whitespace=nowarn "$patch_file"
current_tree="$(worktree_tree)"
if [[ "$current_tree" != "$target_tree" ]]; then
    echo "prepared libultraship tree mismatch: expected ${target_tree}, got ${current_tree}" >&2
    exit 1
fi

echo "libultraship Steam Frame source prepared (${current_tree})"
