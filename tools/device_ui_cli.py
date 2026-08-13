#!/usr/bin/env python3
"""Small stdlib-only utility for testing the firmware UI HTTP API."""
from __future__ import annotations
import argparse
import http.cookiejar
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path


def opener_for(host: str):
    if not host.startswith(("http://", "https://")):
        host = "http://" + host
    base = host.rstrip('/') + '/'
    jar = http.cookiejar.CookieJar()
    return base, urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))


def login(base: str, opener, password: str):
    body = urllib.parse.urlencode({"password": password}).encode()
    req = urllib.request.Request(base + "login", data=body, method="POST")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    with opener.open(req, timeout=15) as r:
        r.read()


def get_json(base: str, opener, path: str):
    with opener.open(base + path.lstrip('/'), timeout=15) as r:
        return json.loads(r.read().decode("utf-8"))


def upload(base: str, opener, path: Path):
    data = path.read_bytes()
    boundary = "----BMSUI" + uuid.uuid4().hex
    head = (
        f"--{boundary}\r\n"
        f"Content-Disposition: form-data; name=\"ui\"; filename=\"{path.name}\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n"
    ).encode("utf-8")
    tail = f"\r\n--{boundary}--\r\n".encode("ascii")
    req = urllib.request.Request(base + "api/ui/upload", data=head + data + tail, method="POST")
    req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
    with opener.open(req, timeout=30) as r:
        return json.loads(r.read().decode("utf-8"))


def post_empty(base: str, opener, path: str):
    req = urllib.request.Request(base + path.lstrip('/'), data=b"", method="POST")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    with opener.open(req, timeout=15) as r:
        return json.loads(r.read().decode("utf-8"))


def download(base: str, opener, output: Path):
    with opener.open(base + "api/ui/download", timeout=15) as r:
        output.write_bytes(r.read())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--password", default="123456")
    action = ap.add_mutually_exclusive_group(required=True)
    action.add_argument("--info", action="store_true")
    action.add_argument("--upload", type=Path)
    action.add_argument("--download", type=Path)
    action.add_argument("--reset", action="store_true")
    args = ap.parse_args()

    base, op = opener_for(args.host)
    try:
        login(base, op, args.password)
        if args.info:
            print(json.dumps(get_json(base, op, "api/ui/info"), ensure_ascii=False, indent=2))
        elif args.upload:
            print(json.dumps(upload(base, op, args.upload), ensure_ascii=False, indent=2))
        elif args.download:
            download(base, op, args.download)
            print(f"saved: {args.download}")
        elif args.reset:
            print(json.dumps(post_empty(base, op, "api/ui/reset"), ensure_ascii=False, indent=2))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        print(f"HTTP {exc.code}: {body}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
