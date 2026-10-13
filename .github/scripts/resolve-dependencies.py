#!/usr/bin/env python3
"""Resolve CI dependencies and verify the exact Psi API checkout."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def matches(value, pattern):
    return isinstance(value, str) and re.fullmatch(pattern, value) is not None


def read_lock(path):
    lock = json.loads(Path(path).read_text(encoding="utf-8"))
    require(lock["schema_version"] == 1, "Unsupported dependency lock schema")
    psi = lock["psi"]
    require(matches(psi["repository"], r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+"), "Invalid Psi repository")
    require(matches(psi["commit"], r"[0-9a-f]{40}"), "Psi must use a full Git commit SHA")
    require(set(lock["windows"]) == {"modern", "win7"}, "Expected modern and win7 profiles")
    for name, profile in lock["windows"].items():
        qt = profile["qt"]
        major = 5 if name == "win7" else 6
        require(qt["major"] == major, "Incorrect Qt major for " + name)
        require(matches(qt["version"], rf"{major}\.\d+\.\d+"), "Invalid Qt version for " + name)
        require(matches(qt["arch"], r"win64_msvc\d+_64"), "Invalid Qt MSVC architecture")
        require(matches(profile["msvc_toolset"], r"(?:\d+\.\d+)?"), "Invalid MSVC toolset")
        require(name != "win7" or profile["msvc_toolset"], "Win7 requires an explicit legacy toolset")
        gst = profile["gstreamer"]
        require(matches(gst["version"], r"\d+\.\d+\.\d+"), "Invalid GStreamer version")
        require(gst["base_url"] == f'https://gstreamer.freedesktop.org/pkg/windows/{gst["version"]}/msvc',
                "Unexpected GStreamer download source")
        version = tuple(map(int, gst["version"].split(".")))
        require(name != "win7" or version < (1, 26, 0), "Win7 cannot use modern Windows GStreamer binaries")
        require(gst["installer_kind"] in {"msi", "inno"}, "Unknown GStreamer installer kind")
        stem = f'gstreamer-1.0-msvc-x86_64-{gst["version"]}'
        expected = ([stem + ".exe"] if gst["installer_kind"] == "inno" else
                    [stem + ".msi", f'gstreamer-1.0-devel-msvc-x86_64-{gst["version"]}.msi'])
        require([item["file"] for item in gst["installers"]] == expected,
                "GStreamer installer names do not match version and kind")
        for item in gst["installers"]:
            require(matches(item["sha256"], r"[0-9a-f]{64}"), "Invalid installer SHA256")
    return lock


def resolve(lock, profile="", requested_psi_ref=""):
    require(not requested_psi_ref or requested_psi_ref == lock["psi"]["commit"],
            "Requested Psi ref disagrees with dependencies.lock.json; update the lock instead")
    values = {"PSI_API_REPOSITORY": lock["psi"]["repository"], "PSI_API_REF": lock["psi"]["commit"]}
    if profile:
        selected = lock["windows"][profile]
        qt, gst = selected["qt"], selected["gstreamer"]
        fingerprint = hashlib.sha256(json.dumps(gst, sort_keys=True).encode()).hexdigest()
        values.update(QT_MAJOR=str(qt["major"]), QT_VERSION=qt["version"], QT_ARCH=qt["arch"],
                      MSVC_TOOLSET=selected["msvc_toolset"], GSTREAMER_VERSION=gst["version"],
                      GSTREAMER_CACHE_KEY=fingerprint)
    return values


def verify_psi(lock, directory):
    directory = Path(directory)
    head = subprocess.check_output(["git", "-C", str(directory), "rev-parse", "HEAD"], text=True).strip()
    require(head == lock["psi"]["commit"], "Psi checkout does not match the locked commit")
    changes = subprocess.check_output(["git", "-C", str(directory), "status", "--porcelain", "--",
                                       "plugins/include"], text=True)
    require(not changes, "Locked Psi API headers have local modifications")
    require((directory / "plugins/include/psimediaprovider.h").is_file(), "Psi media API headers are missing")
    print(f"Verified Psi API checkout: {head}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock-path", type=Path, default=Path("dependencies.lock.json"))
    parser.add_argument("--profile", choices=("modern", "win7"), default="")
    parser.add_argument("--psi-ref", default="")
    parser.add_argument("--verify-psi-dir", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    lock = read_lock(args.lock_path)
    values = resolve(lock, args.profile, args.psi_ref)
    if args.verify_psi_dir:
        verify_psi(lock, args.verify_psi_dir)
    if args.json:
        print(json.dumps({"values": values, "gstreamer": lock["windows"][args.profile]["gstreamer"]
                          if args.profile else None}))
        return
    output = "".join(f"{key}={value}\n" for key, value in values.items())
    for target in ("GITHUB_ENV", "GITHUB_OUTPUT"):
        if os.environ.get(target):
            with open(os.environ[target], "a", encoding="utf-8") as stream:
                stream.write(output)
    print(output, end="")


if __name__ == "__main__":
    try:
        main()
    except (OSError, KeyError, TypeError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f"Invalid dependency lock: {error}")
