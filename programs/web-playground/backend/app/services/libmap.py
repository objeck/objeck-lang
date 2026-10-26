"""Which libraries does a program need, and may the playground supply them?

Users should not have to know that `use Data.JSON;` means `-lib json`, or that
`use Query.Structured;` also needs csv, xml, regex, json, misc, net and cipher
linked. This module works that out from the source.

Source of truth
---------------
`libmap_data.json` is generated (tools/gen_libmap.py) from

  * the `bundle X { ... }` declarations in core/compiler/lib_src/*.obs, which say
    which library provides which bundle and public class, and
  * the `-lib` list each library is built with in core/compiler/build_libs.sh,
    which is its dependency list. obc loads exactly the libraries it is given and
    does not chase a library's own dependencies ("Unable to resolve external
    library class: 'Data.CSV.CsvTable'. Add it with '-lib csv'"), so the playground
    links the whole closure.

The *policy* (what the sandbox permits) lives in app.config and in the tables at
the bottom of this file. Nothing here hard-codes a bundle name.

Libraries come in four kinds:

  default     lang and gen_collect: the compiler always supplies them
  allowed     a program may use them (config.allowed_libs)
  dependency  linked only because an allowed library needs them (config.dependency_libs,
              today just `net`); referring to their bundles directly is refused
  blocked     refused with an explanation (config.blocked_libs, or anything unlisted)
"""

import json
import re
from dataclasses import dataclass, field
from pathlib import Path

from app.config import settings
from app.services.source_scan import Scanned, scan

_DATA = json.loads((Path(__file__).parent / "libmap_data.json").read_text(encoding="utf-8"))

DEFAULT_LIBS: tuple[str, ...] = tuple(_DATA["default_libs"])
LIB_DEPS: dict[str, tuple[str, ...]] = {n: tuple(v["deps"]) for n, v in _DATA["libs"].items()}

# bundle -> libraries that declare it (System.Time is in lang AND misc;
# Web.HTTP is in net, net_h2 and net_quic)
BUNDLE_PROVIDERS: dict[str, list[str]] = {}
# bundle -> {library: class names it contributes}
BUNDLE_CLASSES: dict[str, dict[str, frozenset[str]]] = {}
# class name -> [(bundle, library)]
CLASS_INDEX: dict[str, list[tuple[str, str]]] = {}

for _lib, _info in _DATA["libs"].items():
    for _bundle, _classes in _info["bundles"].items():
        BUNDLE_PROVIDERS.setdefault(_bundle, []).append(_lib)
        BUNDLE_CLASSES.setdefault(_bundle, {})[_lib] = frozenset(_classes)
        for _cls in _classes:
            CLASS_INDEX.setdefault(_cls, []).append((_bundle, _lib))

# Bundles that live inside lang.obl, so no -lib can withhold them. The sandbox
# refuses them by name instead.
LANG_BLOCKED_BUNDLES: dict[str, str] = {
    "System.IO.Filesystem": "it reads and writes files",
    "System.IO.Net": "the sandbox has no network access",
    "System.API": "it loads native libraries",
}

# Why each blocked library is refused. Shown to the user, so write for them.
_BLOCK_REASONS: dict[str, str] = {}
for _libs, _reason in (
    (("net", "net_h2", "net_quic", "net_server", "web_server", "json_rpc", "rss",
      "query_server"), "the sandbox has no network access"),
    (("openai", "gemini", "ollama", "inference"), "it needs network access to a hosted model"),
    (("models", "onnx", "opencv", "query_vision"), "it needs model files and native vision libraries"),
    (("odbc", "query_odbc"), "it needs a database driver and network access"),
    (("sdl2", "sdl_game", "sdl_gl", "lame"), "it needs a display or audio device"),
    (("json_stream",), "it streams from files"),
):
    for _l in _libs:
        _BLOCK_REASONS[_l] = _reason


def canonical_lib(name: str) -> str:
    """The name obc knows a library by: no `.obl`, `collect` is `gen_collect`."""
    name = name.strip().lower()
    if name.endswith(".obl"):
        name = name[:-4]
    return {"collect": "gen_collect"}.get(name, name)


def lib_status(lib: str) -> str:
    """default | allowed | dependency | blocked."""
    if lib in DEFAULT_LIBS:
        return "default"
    if lib in settings.blocked_libs:
        return "blocked"
    if lib in settings.allowed_libs:
        return "allowed"
    if lib in settings.dependency_libs:
        return "dependency"
    return "blocked"


def block_reason(lib: str) -> str:
    return _BLOCK_REASONS.get(lib, "it is not part of the playground")


def link_order(libs: list[str]) -> list[str]:
    """`libs` plus everything they depend on, dependencies first, defaults dropped."""
    ordered: list[str] = []

    def visit(lib: str) -> None:
        if lib in ordered or lib in DEFAULT_LIBS:
            return
        for dep in LIB_DEPS.get(lib, ()):
            visit(dep)
        ordered.append(lib)

    for lib in libs:
        visit(lib)
    return ordered


@dataclass
class Refusal:
    """Something the sandbox will not provide."""
    what: str      # the bundle, class or call as the user wrote it
    reason: str    # short, user-facing
    line: int = 0

    def message(self) -> str:
        where = f" (line {self.line})" if self.line else ""
        return f"{self.what} isn't available in the playground{where}: {self.reason}."


@dataclass
class Analysis:
    bundles: list[str] = field(default_factory=list)      # known bundles the program names
    libs: list[str] = field(default_factory=list)         # non-default libraries it needs
    link_libs: list[str] = field(default_factory=list)    # libs plus dependencies, link order
    refusals: list[Refusal] = field(default_factory=list)
    unknown_bundles: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.refusals


# ---------------------------------------------------------------- detection

# `use` opens a statement: at the start of a line or after `;` / `}`. That keeps
# the word inside a string ("use Data.JSON;") from counting as an import.
_USE_STMT = re.compile(r"(?:^|[;}])[ \t]*use\s+(?!class\b)(?:bundle\s+)?([\w.]+(?:\s*,\s*[\w.]+)*)\s*;", re.M)
_BUNDLE_DECL = re.compile(r"(?<![\w.$])bundle\s+([\w.]+)\s*\{")
_WORD = re.compile(r"[A-Za-z_]\w*")


# A dotted name such as Data.JSON.JsonParser or System.IO.Net.TCPSocket
_DOTTED = re.compile(r"(?<![\w.$])[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)+")


def _qualified_bundle(token: str) -> str | None:
    """The bundle a fully qualified name refers to: its longest proper prefix that
    is a bundle. `Data.JSON.Scheme.Type` -> Data.JSON.Scheme; `Data.JSON` itself,
    being a bundle name and not a class in one, -> None."""
    parts = token.split(".")
    for end in range(len(parts) - 1, 0, -1):
        prefix = ".".join(parts[:end])
        if prefix in BUNDLE_PROVIDERS:
            return prefix
    return None


def referenced_bundles(scanned: Scanned) -> tuple[dict[str, int], list[str]]:
    """Bundles the source names (bundle -> first line) and `use` targets nobody provides."""
    code = scanned.code
    found: dict[str, int] = {}
    declared = {m.group(1) for m in _BUNDLE_DECL.finditer(code)}
    unknown: list[str] = []

    use_spans: list[tuple[int, int]] = []
    for m in _USE_STMT.finditer(code):
        use_spans.append(m.span())
        line = scanned.line_of(m.start(1))
        for name in re.split(r"\s*,\s*", m.group(1)):
            if name in BUNDLE_PROVIDERS:
                found.setdefault(name, line)
            elif name not in declared and not any(d.startswith(name + ".") for d in declared):
                if name not in unknown:
                    unknown.append(name)

    # `Bundle.Class` anywhere: a fully qualified reference needs no `use` line
    for m in _DOTTED.finditer(code):
        if any(a <= m.start() < b for a, b in use_spans):
            continue  # the names a `use` statement lists are imports, not qualified references
        bundle = _qualified_bundle(m.group(0))
        if bundle:
            found.setdefault(bundle, scanned.line_of(m.start()))

    return found, unknown


def analyze(code: str, extra_libs: list[str] | None = None) -> Analysis:
    """Work out the libraries a program needs and what in it the sandbox refuses.

    `extra_libs` are libraries the user asked for by hand (the advanced override).
    """
    scanned = scan(code)
    result = Analysis()
    words = set(_WORD.findall(scanned.code))
    bundles, result.unknown_bundles = referenced_bundles(scanned)
    result.bundles = list(bundles)

    needed: list[str] = []
    for bundle, line in bundles.items():
        providers = BUNDLE_PROVIDERS[bundle]
        if bundle in LANG_BLOCKED_BUNDLES:
            result.refusals.append(Refusal(bundle, LANG_BLOCKED_BUNDLES[bundle], line))
            continue

        # A bundle several libraries contribute to (System.Time: lang has Date and
        # Timer, misc adds DateUtility) needs the extra library only when the
        # program names one of the classes it contributes.
        wanted: list[str] = []
        for lib in providers:
            if lib in DEFAULT_LIBS:
                continue
            if len(providers) == 1 or words & BUNDLE_CLASSES[bundle][lib]:
                wanted.append(lib)
        if not wanted and not any(p in DEFAULT_LIBS for p in providers):
            wanted = providers[:1]

        for lib in wanted:
            status = lib_status(lib)
            if status == "allowed":
                if lib not in needed:
                    needed.append(lib)
            else:
                result.refusals.append(Refusal(bundle, block_reason(lib), line))

    for lib in extra_libs or []:
        lib = canonical_lib(lib)
        if lib_status(lib) == "allowed" and lib not in needed:
            needed.append(lib)

    result.link_libs = link_order(needed)

    # gen_collect is linked by default, so it never needs a -lib; it is still
    # listed as "used" so the UI can say the program relies on Collections.
    if any("gen_collect" in BUNDLE_PROVIDERS[b] for b in bundles):
        needed.insert(0, "gen_collect")
    result.libs = needed
    return result


# ---------------------------------------------------------------- display names

# What the UI calls each user-selectable library. Used for the "libraries used"
# note and the optional advanced override; test_policy.py insists every allowed
# library has an entry.
LIB_INFO: dict[str, tuple[str, str]] = {
    "gen_collect": ("Collections", "Vector, Map, List, Hash and other generic collections"),
    "json": ("JSON", "Parse and build JSON"),
    "xml": ("XML", "Parse XML and query it"),
    "regex": ("RegEx", "Regular expressions"),
    "cipher": ("Cipher", "Hashing, AES and Base64"),
    "csv": ("CSV", "Read and write delimited text"),
    "query": ("Query", "In-memory tables queried with SQL"),
    "query_ml": ("Query ML", "Models as query sources"),
    "ml": ("ML", "Machine learning and matrix math"),
    "ml_lab": ("ML Lab", "Stratified splits, imbalance resampling, parameter sweeps, result tables and SVG charts"),
    "nlp": ("NLP", "Tokenizing, stemming and sentiment"),
    "ai": ("AI", "Search, games, optimization and reinforcement learning"),
    "math": ("Math", "Exact arithmetic"),
    "misc": ("Utilities", "Dates, parsing and formatting helpers"),
    "diags": ("Diagnostics", "Parse and analyze Objeck code"),
    "concurrent": ("Concurrency", "Tasks, semaphores and latches"),
}


def library_catalog() -> list[dict]:
    """The libraries a program may use, for GET /api/libs."""
    result = []
    for lib in sorted(settings.allowed_libs):
        if lib in DEFAULT_LIBS:
            continue  # always linked; nothing for a user to choose
        name, description = LIB_INFO.get(lib, (lib, ""))
        bundles = sorted(b for b, libs in BUNDLE_PROVIDERS.items() if lib in libs)
        result.append({"id": lib, "name": name, "description": description, "bundles": bundles})
    return result


# ------------------------------------------------------------------- hints

def missing_bundle_hint(output: str, analysis: Analysis) -> str:
    """Turn an obc 'Undefined class' / 'Bundle ... not defined' into advice.

    obc cannot suggest a bundle for a class in a library that was not linked,
    which is exactly the case when a program forgot its `use` line.
    """
    import difflib

    hints: list[str] = []
    seen: set[str] = set()

    for name in re.findall(r"Undefined class: '([\w.$]+)'", output):
        if name in seen:
            continue
        seen.add(name)
        short = name.split(".")[-1]
        options = CLASS_INDEX.get(short, [])
        if not options:
            close = difflib.get_close_matches(short, list(CLASS_INDEX), n=1, cutoff=0.8)
            if close and close[0] != short:
                hints.append(f"'{short}' is not a known class. Did you mean '{close[0]}'?")
            continue
        for bundle, lib in options:
            if bundle in analysis.bundles:
                continue
            if bundle in LANG_BLOCKED_BUNDLES:
                hints.append(Refusal(f"{short} ({bundle})", LANG_BLOCKED_BUNDLES[bundle]).message())
            elif lib_status(lib) in ("allowed", "default"):
                hints.append(f"'{short}' is in bundle {bundle}. Add `use {bundle};` at the top of your program.")
            else:
                hints.append(Refusal(f"{short} ({bundle})", block_reason(lib)).message())
            break

    for name in re.findall(r"Bundle name '([\w.]+)' not defined", output):
        close = difflib.get_close_matches(name, list(BUNDLE_PROVIDERS), n=1, cutoff=0.6)
        if close:
            hints.append(f"No library provides bundle '{name}'. Did you mean '{close[0]}'?")
        else:
            hints.append(f"No library provides bundle '{name}'. Check the spelling of your `use` line.")

    return "\n".join(hints[:4])
