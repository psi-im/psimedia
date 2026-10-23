#!/usr/bin/env python3
"""Keep embedded PsiMedia plugin metadata in sync with a release tag."""

import argparse
import json
import re
import sys
from pathlib import Path

TAG_PATTERN = re.compile(r"^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:\.(0|[1-9][0-9]*))?$")
VERSION_FIELD = re.compile(r'(?m)^([ \t]*"version"[ \t]*:[ \t]*")([^"\r\n]*)(")')


def release_version(tag: str) -> str:
    if not TAG_PATTERN.fullmatch(tag):
        raise ValueError("Expected a version tag such as v1.2 or v1.2.3")
    return tag[1:]


def sync_metadata(text: str, tag: str, check: bool) -> str:
    version = release_version(tag)
    metadata = json.loads(text)
    if not isinstance(metadata, dict) or not isinstance(metadata.get("version"), str):
        raise ValueError("Plugin metadata must contain a string version")

    matches = list(VERSION_FIELD.finditer(text))
    if len(matches) != 1 or matches[0].group(2) != metadata["version"]:
        raise ValueError("Expected exactly one literal top-level version field")

    if check:
        if metadata["version"] != version:
            raise ValueError(
                f"Plugin metadata version {metadata['version']!r} does not match tag {tag!r}"
            )
        return text

    match = matches[0]
    updated = text[:match.start(2)] + version + text[match.end(2):]
    if json.loads(updated)["version"] != version:
        raise ValueError("Failed to update plugin metadata")
    return updated


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True, help="Release tag, for example v1.2.3")
    parser.add_argument("--check", action="store_true", help="Validate without changing metadata")
    parser.add_argument("path", nargs="?", default="psiplugin/psiplugin.json",
                        help="Plugin JSON, or '-' for stdin in --check mode")
    args = parser.parse_args()

    if args.path == "-":
        if not args.check:
            parser.error("Reading stdin requires --check")
        text = sys.stdin.read()
    else:
        text = Path(args.path).read_text(encoding="utf-8")

    try:
        updated = sync_metadata(text, args.tag, args.check)
    except ValueError as exc:
        parser.error(str(exc))

    if not args.check and updated != text:
        Path(args.path).write_text(updated, encoding="utf-8")
    print(f"PsiMedia plugin version: {release_version(args.tag)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
