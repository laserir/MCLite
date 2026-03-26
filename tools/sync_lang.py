#!/usr/bin/env python3
"""Sync language files from sdcard/mclite/lang/ into the config tool HTML.

Reads the JSON lang files and regenerates the LANG_FILES constant in
tools/config-tool/mclite_config_tool.html. Keeps the config tool as a
self-contained offline file while using sdcard/ as the single source of truth.

Usage:
    python3 tools/sync_lang.py          # from repo root
    python3 tools/sync_lang.py --check  # exit 1 if out of sync (for CI)
"""

import glob
import json
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANG_DIR = os.path.join(REPO_ROOT, 'sdcard', 'mclite', 'lang')
HTML_PATH = os.path.join(REPO_ROOT, 'tools', 'config-tool', 'mclite_config_tool.html')

MARKER = re.compile(
    r'(// ===== Embedded Language Files =====\n'
    r'// Bundled into export ZIPs so users get translations without a separate download\.\n'
    r'// Source: sdcard/mclite/lang/\*\.json — update here when source files change\.\n)'
    r'const LANG_FILES = \{.*?\};',
    re.DOTALL
)


def build_lang_files_block():
    """Read lang JSONs and build the JS constant."""
    lang_files = sorted(glob.glob(os.path.join(LANG_DIR, '*.json')))
    if not lang_files:
        print(f'Error: no .json files found in {LANG_DIR}', file=sys.stderr)
        sys.exit(1)

    entries = []
    for path in lang_files:
        name = os.path.basename(path)
        with open(path, 'r', encoding='utf-8') as f:
            content = f.read()
        # Validate it's valid JSON
        json.loads(content)
        # json.dumps produces a properly escaped double-quoted JS-compatible string
        escaped = json.dumps(content)
        entries.append(f"  '{name}': {escaped}")

    inner = ',\n'.join(entries)
    return f'const LANG_FILES = {{\n{inner}\n}};'


def main():
    check_only = '--check' in sys.argv

    with open(HTML_PATH, 'r', encoding='utf-8') as f:
        html = f.read()

    if not MARKER.search(html):
        print('Error: could not find LANG_FILES marker in config tool HTML', file=sys.stderr)
        sys.exit(1)

    new_block = build_lang_files_block()
    # Use lambda to avoid re.sub interpreting \n in replacement as newlines
    new_html = MARKER.sub(lambda m: m.group(1) + new_block, html)

    if check_only:
        if html == new_html:
            print('Lang files are in sync.')
            sys.exit(0)
        else:
            print('Lang files are OUT OF SYNC. Run: python3 tools/sync_lang.py', file=sys.stderr)
            sys.exit(1)

    if html == new_html:
        print('Already in sync, no changes needed.')
        return

    with open(HTML_PATH, 'w', encoding='utf-8') as f:
        f.write(new_html)

    print(f'Updated LANG_FILES in {os.path.relpath(HTML_PATH, REPO_ROOT)}')


if __name__ == '__main__':
    main()
