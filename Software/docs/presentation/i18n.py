"""Language switch for the presentation material (draw.io build scripts and Manim scenes).

English is the source language and the only one used in the repo documentation. Set SNA_LANG=de to build the German
slides/videos (exported only). Strings are looked up whole first, then line by line; anything without an entry stays
English and is reported (SNA_I18N_REPORT=<file> appends them, one per line).
"""
import os
import re
from html import escape, unescape

LANG = os.environ.get("SNA_LANG", "en").lower()

if LANG == "de":
    from i18n_de import DE as _TABLE, PATTERNS as _PATTERNS
else:
    _TABLE, _PATTERNS = {}, []

_missing = set()
_WORD = re.compile(r"[A-Za-zÄÖÜäöü]{2,}")


def _lookup(s):
    if s in _TABLE:
        return _TABLE[s]
    for pat, rep in _PATTERNS:
        m = re.fullmatch(pat, s)
        if m:
            return m.expand(rep)
    return None


_DECIMAL = re.compile(r"(?<=\d)\.(?=\d)")


def _localize(s):
    """German number format: 3.3 V -> 3,3 V. JSON and version numbers keep their dots."""
    if "{" in s or '":' in s or re.search(r"[Ff]irmware \d", s):
        return s
    return _DECIMAL.sub(",", s)


def tr(s):
    """Translate a plain-text string (may contain newlines)."""
    if LANG == "en" or not s:
        return s
    return _localize(_tr(s))


def _tr(s):
    if not _WORD.search(s):
        return s
    hit = _lookup(s)
    if hit is not None:
        return hit
    if "\n" in s:
        return "\n".join(tr(line) for line in s.split("\n"))
    core = s.strip()
    if core != s:
        hit = _tr(core)
        lead = s[:len(s) - len(s.lstrip())]
        trail = s[len(s.rstrip()):]
        return lead + hit + trail
    if s not in _missing:
        _missing.add(s)
        path = os.environ.get("SNA_I18N_REPORT")
        if path:  # written at once: manim does not always run atexit hooks
            with open(path, "a", encoding="utf-8") as f:
                f.write(s + "\n")
    return s


def tr_exact(s):
    """Translate only on an exact table hit (code-font text: JSON, formulas); no number reformatting."""
    return s if LANG == "en" else _TABLE.get(s, s)


_TAG = re.compile(r"(<[^>]*>|&emsp;|&ensp;|&nbsp;)")


def tr_html(value):
    """Translate the text between the tags of a draw.io HTML label."""
    if LANG == "en" or not value:
        return value
    parts = _TAG.split(value)
    for i in range(0, len(parts), 2):
        if parts[i]:
            parts[i] = escape(tr(unescape(parts[i])), quote=False)
    return "".join(parts)
