#!/usr/bin/env python3
import subprocess
import sys
import os

def ensure(pkgs):
    for mod, pip in pkgs:
        try:
            __import__(mod)
        except ImportError:
            subprocess.check_call([sys.executable, "-m", "pip", "install", pip])

ensure([
    ("requests", "requests"),
    ("bs4", "beautifulsoup4"),
    ("mutagen", "mutagen"),
])

import io
import json
import html
import re
from urllib.parse import urlparse, unquote
from concurrent.futures import ThreadPoolExecutor, as_completed

import requests
from bs4 import BeautifulSoup
from mutagen.oggopus import OggOpus

SESSION = requests.Session()
SESSION.headers["Connection"] = "keep-alive"

DEFAULT_OUTPUT = os.path.join(os.path.expanduser("~"), "Desktop", "music.json")


def ask(prompt, default=None):
    try:
        if default is None:
            return input(f"{prompt}: ").strip()
        return input(f"{prompt} [{default}]: ").strip() or default
    except (EOFError, KeyboardInterrupt):
        raise SystemExit


def ask_int(prompt, default=4):
    try:
        raw = input(f"{prompt} [{default}]: ").strip()
        return int(raw) if raw else default
    except (ValueError, EOFError, KeyboardInterrupt):
        return default


def setup():
    cdn = ask("CDN URL")
    if not cdn:
        raise SystemExit
    if not cdn.endswith("/"):
        cdn += "/"
    out = ask("output path", DEFAULT_OUTPUT)
    title = ask("json title") or "music"
    workers = ask_int("threads", 4)
    upper = {w.strip().lower() for w in ask("uppercase words").split(",") if w.strip()}

    print("\nBuild categories. Type a name, then the keywords that")
    print("make a file land in that category. Blank name to finish.")
    print("\nExample:")
    print("    category name: Visual Novel")
    print("    keywords: vn, visual novel")
    print("    -> any file with 'vn' or 'visual novel' in its name goes there")
    print("\nFirst match wins, so put specific categories before general ones.\n")

    rules = []
    while True:
        try:
            cat = input("category name (blank to finish): ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if not cat:
            break
        try:
            raw = input(f"  keywords for '{cat}' (comma-separated): ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        keys = [k.strip().lower() for k in raw.split(",") if k.strip()]
        if not keys:
            print("  no keywords, skipping")
            continue
        rules.append((cat, keys))
        print(f"  added: {cat} <- {', '.join(keys)}")

    print("\nForced files go straight to a category, no keyword check.")
    print("Example:  umineko golden fantasia - luminous.opus -> Umineko Golden Fantasia OST")
    print("Blank line to finish.\n")

    forced = {}
    while True:
        try:
            line = input("file> ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if not line:
            break
        if "->" not in line:
            print("  format is: filename -> Category Name")
            continue
        name, _, cat = line.partition("->")
        name, cat = name.strip().lower(), cat.strip()
        if name and cat:
            forced[name] = cat
        else:
            print("  you need a filename AND a category")

    print(f"\n{cdn}\n{out}\n{len(rules)} categories, {len(forced)} forced files")
    try:
        if input("go? [Y/n]: ").strip().lower() in ("n", "no"):
            raise SystemExit
    except (EOFError, KeyboardInterrupt):
        raise SystemExit

    return cdn, out, title, workers, upper, rules, forced


def format_title(name, upper):
    try:
        ext = os.path.splitext(name)[1]
        base = name[:-len(ext)] if ext else name

        def cap(w):
            if not w:
                return w
            if w.lower() in upper:
                return w.upper()
            if w.isupper() and len(w) > 1:
                return w
            return w[0].upper() + w[1:].lower() if w[0].isalpha() else w

        parts = re.split(r"(\s+|[\[\]\-_:])", base)
        return "".join(
            cap(p) if p.strip() and not re.match(r"\s+|[\[\]\-_:]", p) else p
            for p in parts
        ) + ext.lower()
    except Exception:
        return name


def list_files(cdn, ext):
    print(f"\nfetching {cdn}")
    try:
        r = SESSION.get(cdn, timeout=20)
        r.raise_for_status()
    except Exception as e:
        print(f"could not fetch index: {e}")
        return []

    out = []
    try:
        soup = BeautifulSoup(r.text, "html.parser")
    except Exception:
        return []

    for a in soup.find_all("a"):
        try:
            href = a.get("href")
            if not href or not href.lower().endswith(ext):
                continue
            raw = os.path.basename(urlparse(href).path)
            try:
                name = bytes(unquote(raw), "latin1").decode("utf-8")
            except (UnicodeEncodeError, UnicodeDecodeError):
                name = unquote(raw)
            name = html.unescape(name).strip().lower()
            if name:
                out.append((name, cdn + name))
        except Exception:
            continue

    print(f"{len(out)} files")
    return out


def duration(url, partial):
    try:
        r = SESSION.get(url, headers={"Range": f"bytes=0-{partial - 1}"}, timeout=20)
        r.raise_for_status()
        return round(OggOpus(io.BytesIO(r.content)).info.length, 2)
    except Exception:
        pass
    try:
        r = SESSION.get(url, timeout=60)
        r.raise_for_status()
        return round(OggOpus(io.BytesIO(r.content)).info.length, 2)
    except Exception:
        return -1


def durations(files, workers, partial):
    print(f"\n{workers} threads")
    out = {}
    try:
        with ThreadPoolExecutor(workers) as pool:
            futs = {pool.submit(duration, url, partial): name for name, url in files}
            for i, f in enumerate(as_completed(futs), 1):
                name = futs[f]
                try:
                    out[name] = f.result()
                except Exception:
                    out[name] = -1
                print(f"[{i}/{len(files)}] {name} - {out[name]}s")
    except Exception as e:
        print(f"thread pool failed: {e}")
    return out


def categorize(name, rules, forced):
    try:
        if name in forced:
            return forced[name]
        for cat, keys in rules:
            if any(k in name for k in keys):
                return cat
    except Exception:
        pass
    return "Other"


def build(files, durs, upper, rules, forced):
    groups = {}
    for name, _ in files:
        try:
            cat = categorize(name, rules, forced)
            groups.setdefault(cat, []).append({
                "name": format_title(name, upper),
                "length": durs.get(name, -1),
            })
        except Exception:
            continue

    for songs in groups.values():
        songs.sort(key=lambda s: s["name"].lower())

    return [
        {"category": c, "songs": s}
        for c, s in sorted(groups.items(), key=lambda x: x[0].lower())
    ]


def main():
    cdn, out, title, workers, upper, rules, forced = setup()

    ext = ask("file extension", ".opus")
    files = list_files(cdn, ext)
    if not files:
        print("nothing found")
        return
    durs = durations(files, workers, 1024 * 1024)
    groups = build(files, durs, upper, rules, forced)
    data = {"title": title, "categories": groups}

    try:
        with open(out, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
    except Exception as e:
        print(f"could not write {out}: {e}")
        return

    total = sum(len(c["songs"]) for c in groups)
    print(f"\nwrote {total} songs in {len(groups)} categories to {out}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\ncancelled")
    except SystemExit:
        pass
    except Exception as e:
        print(f"\nfailed: {e}")