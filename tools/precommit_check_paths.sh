#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
#
# Enforces Constitution C1: no absolute personal paths in versioned files.
# Usage: bash tools/precommit_check_paths.sh
# Exit 0 if clean, 1 if violations found.

set -euo pipefail

# Files to scan (versioned source / docs / config)
DIRS=(src specs docs schemas profiles tests tools shaders .github)

# Patterns considered violations:
#  - Windows: drive letter + \Users\<name>
#  - Linux:   /home/<user>/
#  - macOS:   /Users/<user>/
PATTERN='([A-Z]:\\[Uu]sers\\|/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/)'

# Whitelist:
#  - CI runner paths
#  - Generic placeholders that document the rule
WHITELIST_PATTERN='(/home/runner/|C:.Users.runneradmin|/home/<user>|C:.Users.<user>|/Users/<user>|pathcheck-allow)'

# Files explicitly excluded from scan (self-aware: they describe the rule itself).
EXCLUDE_PATHS=(
    "tools/precommit_check_paths.sh"
    "tools/precommit_check_paths.ps1"
    "specs/CONSTITUTION.LOCKED.md"
    "docs/adr/README.md"
)

is_excluded() {
    local f="$1"
    for ex in "${EXCLUDE_PATHS[@]}"; do
        if [ "$f" = "$ex" ] || [ "$f" = "./$ex" ]; then
            return 0
        fi
    done
    return 1
}

FILES=()
for d in "${DIRS[@]}"; do
    if [ -d "$d" ]; then
        while IFS= read -r -d '' file; do
            # strip leading ./ for consistent comparison
            file="${file#./}"
            if ! is_excluded "$file"; then
                FILES+=("$file")
            fi
        done < <(find "$d" -type f \
            \( -name '*.md' -o -name '*.cpp' -o -name '*.cc' -o -name '*.h' -o -name '*.hpp' \
            -o -name '*.json' -o -name '*.glsl' -o -name '*.frag' -o -name '*.vert' -o -name '*.comp' \
            -o -name '*.cmake' -o -name 'CMakeLists.txt' -o -name '*.txt' \
            -o -name '*.yml' -o -name '*.yaml' -o -name '*.toml' -o -name '*.sh' -o -name '*.ps1' \) \
            -print0)
    fi
done

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "[precommit] no files to scan."
    exit 0
fi

HITS=$(grep -nIEH "$PATTERN" "${FILES[@]}" 2>/dev/null | grep -vE "$WHITELIST_PATTERN" || true)

if [ -n "$HITS" ]; then
    echo "ERROR (C1 violation): absolute personal paths found in versioned files:"
    echo "$HITS"
    echo
    echo "Fix: replace with relative paths, env vars (e.g. \$HOME, %APPDATA%), or a placeholder."
    exit 1
fi

echo "[precommit] OK — no personal paths in versioned files."
exit 0
