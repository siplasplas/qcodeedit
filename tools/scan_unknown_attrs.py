#!/usr/bin/env python3
"""Scan Kate syntax XML files and report attributes not handled by KateXmlReader."""

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION_HEADER = os.path.join(REPO_ROOT, "kate", "include", "qce", "kate",
                              "KateSyntaxVersion.h")


def supported_syntax_version():
    """Read supportedSyntaxVersion() from KateSyntaxVersion.h, e.g. "6.31"."""
    with open(VERSION_HEADER, encoding="utf-8") as f:
        m = re.search(r"supportedSyntaxVersion\(\)\s*\{\s*return\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}",
                      f.read())
    if not m:
        sys.exit(f"Cannot find supportedSyntaxVersion() in {VERSION_HEADER}")
    return f"{m.group(1)}.{m.group(2)}"


def generic_data_location():
    """Mirror QStandardPaths::GenericDataLocation (writable)."""
    if sys.platform == "win32":
        return os.environ.get("LOCALAPPDATA", os.path.expanduser("~/AppData/Local"))
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Application Support")
    return os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")


def default_syntax_dir():
    """Same resolution as qce::kate::syntaxDir()."""
    data_dir = os.environ.get("QCE_KATE_DATA_DIR") or os.path.join(
        generic_data_location(), "qcodeedit", f"kate-{supported_syntax_version()}")
    return os.path.join(data_dir, "syntax")

# Attributes that KateXmlReader explicitly reads for each element type.
KNOWN_ATTRS = {
    "language":   {"name"},
    "keywords":   {"casesensitive"},
    "list":       {"name"},
    "item":       set(),
    "context":    {"name", "attribute", "lineEndContext",
                   "fallthrough", "fallthroughContext"},
    "itemData":   {"name", "defStyleNum", "italic", "bold", "underline",
                   "strikeOut", "color"},
    # Rule tags — common set read for every rule:
    "_rule_common": {"attribute", "context", "lookAhead", "firstNonSpace",
                     "insensitive", "beginRegion", "endRegion", "column"},
    # Rule-specific extra attributes:
    "DetectChar":       {"char"},
    "Detect2Chars":     {"char", "char1"},
    "AnyChar":          {"String"},
    "StringDetect":     {"String"},
    "WordDetect":       {"String"},
    "RegExpr":          {"String"},
    "keyword":          {"String"},
    "RangeDetect":      {"char", "char1"},
    "IncludeRules":     {"context"},
    "Int":              set(),
    "Float":            set(),
    "HlCStringChar":    set(),
    "HlCChar":          set(),
    "HlCOct":           set(),
    "HlCHex":           set(),
    "LineContinue":     {"char"},
    "DetectSpaces":     set(),
    "DetectIdentifier": set(),
}

# Rule tag names that KateXmlReader handles (all others are skipped).
KNOWN_RULE_TAGS = {
    "DetectChar", "Detect2Chars", "AnyChar", "StringDetect", "WordDetect",
    "RegExpr", "keyword", "DetectSpaces", "DetectIdentifier", "Int", "Float",
    "HlCStringChar", "HlCChar", "HlCOct", "HlCHex",
    "LineContinue", "RangeDetect", "IncludeRules",
}


def full_rule_known(tag):
    return KNOWN_ATTRS["_rule_common"] | KNOWN_ATTRS.get(tag, set())


def expand_entities(text):
    """Remove the DTD block so ElementTree can parse; entity refs become empty."""
    import re
    # Strip internal subset (<!DOCTYPE ... [ ... ]>)
    text = re.sub(r'<!DOCTYPE[^[]*\[[^\]]*\]>', '', text, flags=re.DOTALL)
    # Replace remaining &foo; entity refs (non-standard) with empty string
    text = re.sub(r'&(?!amp;|lt;|gt;|apos;|quot;)[A-Za-z_][A-Za-z0-9_]*;', '', text)
    return text


def scan_file(path):
    """Return (unknown_attrs, unknown_rule_tags) dicts for one file."""
    unknown_attrs = defaultdict(lambda: defaultdict(int))  # tag -> attr -> count
    unknown_rule_tags = defaultdict(int)                   # tag -> count

    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            raw = f.read()
        src = expand_entities(raw)
        root = ET.fromstring(src)
    except ET.ParseError as e:
        print(f"  PARSE ERROR in {os.path.basename(path)}: {e}", file=sys.stderr)
        return unknown_attrs, unknown_rule_tags

    for elem in root.iter():
        tag = elem.tag
        attrs = set(elem.attrib.keys())

        if tag in ("language", "keywords", "list", "item", "itemData"):
            known = KNOWN_ATTRS.get(tag, set())
            for a in attrs - known:
                unknown_attrs[tag][a] += 1

        elif tag == "context":
            known = KNOWN_ATTRS["context"]
            for a in attrs - known:
                unknown_attrs["context"][a] += 1

        elif tag in KNOWN_RULE_TAGS:
            known = full_rule_known(tag)
            for a in attrs - known:
                unknown_attrs[tag][a] += 1

        elif tag not in ("highlighting", "general", "comment", "comments",
                         "folding", "indentation", "spellchecking",
                         "emptyLines", "emptyLine"):
            # Anything inside <context> that isn't a known rule tag
            parent = None
            # Walk to check if parent is a context (heuristic via tag name)
            # We just collect any unknown rule-like tag inside highlighting
            if tag not in ("highlighting", "language", "general", "contexts",
                           "itemDatas", "lists", "keywords"):
                unknown_rule_tags[tag] += 1

    return unknown_attrs, unknown_rule_tags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dir", default=None,
                        help="directory with Kate *.xml files "
                             "(default: qcodeedit data dir, see qce::kate::syntaxDir())")
    args = parser.parse_args()
    syntax_dir = args.dir or default_syntax_dir()

    if not os.path.isdir(syntax_dir):
        print(f"Syntax directory not found: {syntax_dir}")
        print("Download the data set with qce-kate-fetch, or pass --dir.")
        sys.exit(1)

    files = sorted(f for f in os.listdir(syntax_dir) if f.endswith(".xml"))
    print(f"Scanning {len(files)} files in {syntax_dir}\n")

    total_unknown_attrs = defaultdict(lambda: defaultdict(int))
    total_unknown_tags  = defaultdict(int)

    for fname in files:
        path = os.path.join(syntax_dir, fname)
        ua, ut = scan_file(path)
        for tag, d in ua.items():
            for attr, cnt in d.items():
                total_unknown_attrs[tag][attr] += cnt
        for tag, cnt in ut.items():
            total_unknown_tags[tag] += cnt

    print("=== Unknown attributes on known elements ===")
    if total_unknown_attrs:
        for tag in sorted(total_unknown_attrs):
            attrs = total_unknown_attrs[tag]
            attr_list = ", ".join(
                f"{a}({n})" for a, n in sorted(attrs.items(), key=lambda x: -x[1])
            )
            print(f"  <{tag}>: {attr_list}")
    else:
        print("  (none)")

    print()
    print("=== Unknown / unhandled rule tags ===")
    if total_unknown_tags:
        for tag in sorted(total_unknown_tags, key=lambda t: -total_unknown_tags[t]):
            print(f"  {tag}: {total_unknown_tags[tag]}")
    else:
        print("  (none)")


if __name__ == "__main__":
    main()
