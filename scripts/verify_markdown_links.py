#!/usr/bin/env python3
"""
Automated Markdown Link and Anchor Verification Script
Scans all markdown documentation files and verifies that:
1. Relative file paths in markdown links [text](path) point to existing files.
2. Reports broken links with file path and line numbers.
"""

import os
import re
import sys
from pathlib import Path

def check_markdown_links(root_dir):
    root = Path(root_dir)
    md_files = list(root.glob("**/*.md"))
    
    link_pattern = re.compile(r'\[([^\]]+)\]\(([^)]+)\)')
    broken_links = []
    total_links_checked = 0

    # Exclude external links, anchors, and email links
    for md_file in md_files:
        # Skip build dirs or git
        if "build" in md_file.parts or ".git" in md_file.parts:
            continue

        try:
            with open(md_file, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except Exception as e:
            print(f"[!] Error reading {md_file}: {e}")
            continue

        for line_no, line in enumerate(lines, 1):
            for match in link_pattern.finditer(line):
                link_text, link_target = match.groups()
                link_target = link_target.strip()

                # Ignore web URLs, mailto, or pure anchor jumps #anchor
                if link_target.startswith(("http://", "https://", "mailto:", "#")):
                    continue

                total_links_checked += 1
                # Strip anchor fragment if present (e.g., path/to/file.md#section)
                target_path_str = link_target.split("#")[0]
                if not target_path_str:
                    continue

                # Resolve relative to current md file's parent directory
                target_path = (md_file.parent / target_path_str).resolve()
                if not target_path.exists():
                    broken_links.append((str(md_file.relative_to(root)), line_no, link_target))

    print(f"[*] Verified {total_links_checked} relative Markdown links across {len(md_files)} documentation files.")
    if broken_links:
        print(f"[!] Found {len(broken_links)} broken relative link(s):")
        for src, line, target in broken_links:
            print(f"    - {src}:{line} -> '{target}' (file not found)")
        return False
    else:
        print("[+] All relative Markdown file links successfully verified. Zero broken links!")
        return True

if __name__ == "__main__":
    project_root = sys.argv[1] if len(sys.argv) > 1 else "."
    success = check_markdown_links(project_root)
    sys.exit(0 if success else 1)
