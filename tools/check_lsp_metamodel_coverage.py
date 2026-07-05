#!/usr/bin/env python3
"""Compare LspCpp LSP method coverage against the official LSP metaModel snapshot."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Set, Tuple


DEFAULT_METAMODEL = "tools/lsp-metaModel-3.18.json"
DEFAULT_ALLOWLIST = "tools/lsp-metamodel-allowlist.json"
HEADER_GLOBS = ("include/LibLsp/lsp/**/*.h",)
EXTRA_HEADER_FILES = ("include/LibLsp/JsonRpc/Cancellation.h",)
HANDLER_FILE = "src/lsp/ProtocolJsonHandler.cpp"

ISSUE_CATEGORIES = (
    "missing_type_declaration",
    "missing_request_parser",
    "missing_response_parser",
    "missing_notification_parser",
    "response_only_no_request",
    "duplicate_registrations",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check LspCpp LSP method coverage against the official metaModel snapshot."
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Repository root (default: parent of tools/).",
    )
    parser.add_argument(
        "--metamodel",
        type=Path,
        default=None,
        help=f"Path to metaModel JSON (default: {DEFAULT_METAMODEL}).",
    )
    parser.add_argument(
        "--allowlist",
        type=Path,
        default=None,
        help=f"Path to allowlist JSON (default: {DEFAULT_ALLOWLIST}).",
    )
    parser.add_argument(
        "--warn-only",
        action="store_true",
        help="Always exit 0 after printing the report.",
    )
    parser.add_argument(
        "--write-allowlist",
        action="store_true",
        help="Write the current issue set to the allowlist file and exit 0.",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")


def extract_macros(text: str, macro_name: str) -> List[Tuple[str, str]]:
    results: List[Tuple[str, str]] = []
    needle = macro_name + "("
    index = 0
    while True:
        start = text.find(needle, index)
        if start == -1:
            break
        pos = start + len(needle)
        depth = 1
        while pos < len(text) and depth:
            char = text[pos]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            pos += 1
        block = text[start + len(needle) : pos - 1]
        strings = re.findall(r'"([^"]+)"', block)
        method = strings[-1] if strings else None
        symbol = block.strip().split(",")[0].strip()
        if method:
            results.append((symbol, method))
        index = pos
    return results


def is_valid_method(method: str) -> bool:
    if not method:
        return False
    if method.endswith(".h"):
        return False
    if "LibLsp/" in method or "JsonRpc/" in method:
        return False
    return True


def read_header_text(repo_root: Path) -> str:
    chunks: List[str] = []
    for pattern in HEADER_GLOBS:
        for path in sorted(repo_root.glob(pattern)):
            chunks.append(path.read_text(encoding="utf-8"))
            chunks.append("\n")
    for relative_path in EXTRA_HEADER_FILES:
        path = repo_root / relative_path
        chunks.append(path.read_text(encoding="utf-8"))
        chunks.append("\n")
    return "".join(chunks)


def load_declarations(repo_root: Path) -> Tuple[Dict[str, str], Dict[str, str], Dict[str, str]]:
    text = read_header_text(repo_root)
    declared_requests: Dict[str, str] = {}
    declared_notifications: Dict[str, str] = {}
    # Built from the raw (symbol, method) pairs so that two symbols sharing one
    # method string (e.g. WindowShowMessage / Notify_ShowMessage both using
    # "window/showMessage") each keep their own entry.
    symbol_to_method: Dict[str, str] = {}

    for symbol, method in extract_macros(text, "DEFINE_REQUEST_RESPONSE_TYPE"):
        if is_valid_method(method):
            declared_requests[method] = symbol
            symbol_to_method[symbol] = method

    for symbol, method in extract_macros(text, "DEFINE_NOTIFICATION_TYPE"):
        if is_valid_method(method):
            declared_notifications[method] = symbol
            symbol_to_method[symbol] = method

    return declared_requests, declared_notifications, symbol_to_method


def load_metamodel_methods(metamodel_path: Path) -> Tuple[Set[str], Set[str], str]:
    metamodel = load_json(metamodel_path)
    version = metamodel.get("metaData", {}).get("version", "unknown")
    standard_requests = {entry["method"] for entry in metamodel.get("requests", [])}
    standard_notifications = {entry["method"] for entry in metamodel.get("notifications", [])}
    return standard_requests, standard_notifications, version


def extract_handler_registrations(
    handler_text: str, symbol_to_method: Dict[str, str]
) -> Dict[str, List[str]]:
    registrations = {
        "method2request": [],
        "method2response": [],
        "method2notification": [],
    }
    pattern = re.compile(
        r"handler\.(method2request|method2response|method2notification)"
        r"\[([^\]]+)::(?:request|notify)::kMethodInfo\]"
    )
    for map_name, symbol_expr in pattern.findall(handler_text):
        symbol = symbol_expr.strip()
        method = symbol_to_method.get(symbol, f"<unknown:{symbol}>")
        registrations[map_name].append(method)
    return registrations


def is_standard_method(method: str, standard_requests: Set[str], standard_notifications: Set[str]) -> bool:
    return method in standard_requests or method in standard_notifications


def collect_issues(
    declared_requests: Dict[str, str],
    declared_notifications: Dict[str, str],
    registrations: Dict[str, List[str]],
    standard_requests: Set[str],
    standard_notifications: Set[str],
) -> Tuple[Dict[str, List[str]], Dict[str, List[str]]]:
    issues: Dict[str, List[str]] = {category: [] for category in ISSUE_CATEGORIES}
    extensions: Dict[str, List[str]] = defaultdict(list)

    declared_request_methods = set(declared_requests)
    declared_notification_methods = set(declared_notifications)
    declared_all = declared_request_methods | declared_notification_methods

    request_registrations = set(registrations["method2request"])
    response_registrations = set(registrations["method2response"])
    notification_registrations = set(registrations["method2notification"])
    registered_all = request_registrations | response_registrations | notification_registrations

    standard_all = standard_requests | standard_notifications
    for method in sorted(standard_all - declared_all):
        issues["missing_type_declaration"].append(method)

    for method in sorted(declared_request_methods):
        standard = is_standard_method(method, standard_requests, standard_notifications)
        if method not in request_registrations:
            if standard:
                issues["missing_request_parser"].append(method)
            else:
                extensions["missing_request_parser"].append(method)
        if method not in response_registrations:
            if standard:
                issues["missing_response_parser"].append(method)
            else:
                extensions["missing_response_parser"].append(method)
        if method in response_registrations and method not in request_registrations:
            if standard:
                issues["response_only_no_request"].append(method)
            else:
                extensions["response_only_no_request"].append(method)

    for method in sorted(declared_notification_methods):
        if method not in notification_registrations:
            if is_standard_method(method, standard_requests, standard_notifications):
                issues["missing_notification_parser"].append(method)
            else:
                extensions["missing_notification_parser"].append(method)

    for map_name, methods in registrations.items():
        for method, count in sorted(Counter(methods).items()):
            if count > 1:
                entry = f"{map_name}:{method}"
                if is_standard_method(method, standard_requests, standard_notifications):
                    issues["duplicate_registrations"].append(entry)
                else:
                    extensions["duplicate_registrations"].append(entry)

    for method in sorted(declared_all):
        if not is_standard_method(method, standard_requests, standard_notifications):
            extensions["declared"].append(method)

    for method in sorted(registered_all):
        if method.startswith("<unknown:"):
            extensions["unknown_handler_symbol"].append(method)
        elif method not in declared_all and not is_standard_method(
            method, standard_requests, standard_notifications
        ):
            extensions["registered_only"].append(method)

    return issues, extensions


def subtract_allowlist(
    issues: Dict[str, List[str]], allowlist: Dict[str, List[str]]
) -> Tuple[Dict[str, List[str]], Dict[str, List[str]]]:
    remaining: Dict[str, List[str]] = {}
    stale: Dict[str, List[str]] = {}
    for category in ISSUE_CATEGORIES:
        allowed = set(allowlist.get(category, []))
        current = issues.get(category, [])
        filtered = [item for item in current if item not in allowed]
        if filtered:
            remaining[category] = filtered
        stale_items = sorted(allowed - set(current))
        if stale_items:
            stale[category] = stale_items
    return remaining, stale


def print_section(title: str, items: Iterable[str]) -> None:
    items = list(items)
    print(f"[{title}] ({len(items)})")
    if not items:
        print("  (none)")
        return
    for item in items:
        print(f"  - {item}")


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    metamodel_path = (args.metamodel or repo_root / DEFAULT_METAMODEL).resolve()
    allowlist_path = (args.allowlist or repo_root / DEFAULT_ALLOWLIST).resolve()
    handler_path = repo_root / HANDLER_FILE

    standard_requests, standard_notifications, version = load_metamodel_methods(metamodel_path)
    declared_requests, declared_notifications, symbol_to_method = load_declarations(repo_root)
    handler_text = handler_path.read_text(encoding="utf-8")
    registrations = extract_handler_registrations(handler_text, symbol_to_method)
    issues, extensions = collect_issues(
        declared_requests,
        declared_notifications,
        registrations,
        standard_requests,
        standard_notifications,
    )

    if args.write_allowlist:
        payload = {category: issues[category] for category in ISSUE_CATEGORIES}
        write_json(allowlist_path, payload)
        print(f"Wrote allowlist to {allowlist_path}")
        return 0

    allowlist = load_json(allowlist_path) if allowlist_path.exists() else {}
    remaining, stale = subtract_allowlist(issues, allowlist)

    print("LspCpp LSP metaModel coverage report")
    print(f"  metaModel: {metamodel_path} (version {version})")
    print(f"  declared requests: {len(declared_requests)}")
    print(f"  declared notifications: {len(declared_notifications)}")
    print(f"  registered requests: {len(registrations['method2request'])}")
    print(f"  registered responses: {len(registrations['method2response'])}")
    print(f"  registered notifications: {len(registrations['method2notification'])}")
    print()

    print("Standard LSP issues")
    for category in ISSUE_CATEGORIES:
        print_section(category, issues[category])
    print()

    if allowlist_path.exists():
        print("Allowlisted regressions filtered out")
        for category in ISSUE_CATEGORIES:
            allowed = allowlist.get(category, [])
            if allowed:
                print_section(f"allowlisted {category}", allowed)
        print()
        print("Unexpected standard LSP issues")
        if remaining:
            for category, items in remaining.items():
                print_section(category, items)
        else:
            print("  (none)")
        print()
        print("Stale allowlist entries (resolved issues; remove them from the allowlist)")
        if stale:
            for category, items in stale.items():
                print_section(f"stale {category}", items)
        else:
            print("  (none)")
    else:
        print(f"Allowlist not found at {allowlist_path}")
        remaining = issues
        stale = {}

    print()
    print("Extension / vendor methods (informational)")
    for category in sorted(extensions):
        print_section(f"extension {category}", extensions[category])

    if args.warn_only:
        return 0
    return 1 if remaining or stale else 0


if __name__ == "__main__":
    sys.exit(main())
