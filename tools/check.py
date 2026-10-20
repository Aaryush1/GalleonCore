#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

"""Checks of GalleonCore, targeted while working and complete before a merge.

  tools/check.py quick    one small change: incremental build, the tests whose program,
                          library or data changed since they last passed, clang-tidy (results
                          reused for unchanged content), qmllint, reuse
  tools/check.py batch    the end of a batch of changes: quick, plus the same selection under
                          Clang with the sanitizers
  tools/check.py full     what the CI does, on the whole project (all tests under GCC and
                          Clang + ASan/UBSan, fuzzing, clang-tidy everywhere, qmllint, reuse)
  tools/check.py status   is there a full validation for the tree as it is now?
  tools/check.py image    (re)build the Ubuntu 24.04 image of the CI

By default the checks run in a container made from tools/ci/Dockerfile (Ubuntu 24.04: the
compilers, Qt and clang-tidy of the CI), with the build trees and caches in docker volumes
so that the next run only does what changed. `--env host` runs them with the tools of this
machine (faster, but other versions than the CI's).

A targeted run says TARGETED and never counts as a full validation. Changes are counted from
the last tree that passed in full (else from origin/main). A change that touches something
shared (build files, dependencies, CI, the library `lib/`, data) is run in full at once, and
so is one that makes most of the tests out of date. Nothing here relaxes a rule: it only
avoids doing again what cannot have changed.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import fnmatch
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

IMAGE = "galleoncore-ci:24.04"
TIDY_DIRS = ("lib/src/", "daemon/", "bosun/", "tests/", "media/")
LONG_FUZZ_SECONDS = 60
CI_FILE = ".github/workflows/ci.yml"
# The steps of the CI lint job that need no build: replayed as the CI writes them, in a clean
# copy of the tree, so that what one step leaves behind (a cache folder...) is seen by the next.
CI_STEPS_REPLAYED = ("qmllint", "Decisions of tools/check.py", "REUSE licensing")
STALE_SHARE_FOR_FULL = 0.6  # more than this share of the tests stale: a shared header changed

# Where a changed file sends the checks. The first matching pattern wins.
FULL_PATTERNS = (
    ("**/CMakeLists.txt", "build configuration"),
    ("CMakeLists.txt", "build configuration"),
    ("cmake/*", "build configuration"),
    (".clang-tidy", "lint configuration"),
    (".github/*", "CI configuration"),
    ("tools/ci/*", "CI environment"),
    ("tools/check.py", "the checks themselves"),
    ("tools/test_check.py", "the checks themselves"),
    ("tests/lsan.supp", "sanitizer configuration"),
    ("REUSE.toml", "licensing configuration"),
    ("lib/*", "the protocol library is shared and security critical"),
    ("fuzz/*", "the fuzz targets"),
    ("data/*", "device data read by several parts"),
    ("public/*", "assets read by several parts"),
)
CODE_PATTERNS = ("daemon/*", "bosun/*", "tests/*")
QML_PATTERNS = ("widgets/*.qml",)
DOC_PATTERNS = ("*.md", "docs/*", "site/*", "LICENSES/*", ".gitignore", "tools/systemd/*", "tools/udev/*",
                "tools/windows/*", "tools/linux/*", "plasmoid/*", "openrgb-plugin/*")


@dataclasses.dataclass
class Scope:
    kind: str  # none, docs, targeted or full
    reasons: list[str]


def matches(path: str, patterns: tuple[str, ...]) -> bool:
    return any(fnmatch.fnmatch(path, pattern) for pattern in patterns)


def classify(paths: list[str]) -> Scope:
    """What kind of checks a set of changed files calls for. Pure."""
    if not paths:
        return Scope("none", ["nothing changed"])
    full: list[str] = []
    built = False
    for path in sorted(paths):
        reason = next((why for pattern, why in FULL_PATTERNS if fnmatch.fnmatch(path, pattern)), None)
        if reason:
            full.append(f"{path}: {reason}")
        elif matches(path, CODE_PATTERNS) or matches(path, QML_PATTERNS):
            built = True
        elif not matches(path, DOC_PATTERNS):
            full.append(f"{path}: not a kind of file the targeted checks know")
    if full:
        return Scope("full", full)
    return Scope("targeted" if built else "docs", ["only built code, QML or documents changed"])


def tests_using(token: str, tests_dir: Path) -> set[str]:
    """The tests whose source mentions `token` (a data folder, for instance): they read
    files that the build graph does not know about."""
    return {source.stem for source in tests_dir.glob("test_*.cpp") if token in source.read_text(errors="replace")}


def inputs_key(parts: list[bytes]) -> str:
    """One name for everything a test result depends on (its program, the libraries it
    loads, the data it reads): the same name means the same result can be reused. Pure."""
    digest = hashlib.sha256()
    for part in parts:
        digest.update(hashlib.sha256(part).digest())
    return digest.hexdigest()


def stale(now: dict[str, str], passed: dict[str, str]) -> set[str]:
    """The tests that have no recorded pass for what they are now. Pure."""
    return {name for name, key in now.items() if passed.get(name) != key}


def too_many_stale(chosen: set[str], every: set[str]) -> bool:
    """A change that makes most of the tests stale is a shared one: run everything."""
    return bool(every) and len(chosen) / len(every) > STALE_SHARE_FOR_FULL


def parse_steps(text: str) -> dict[str, str]:
    """The `run:` command of each named step of a GitHub workflow file. Pure. (A step may
    run a single line or a `|` block; that is all this reads, not YAML in general.)"""
    steps: dict[str, str] = {}
    name = ""
    lines = text.splitlines()
    for number, line in enumerate(lines):
        named = re.match(r"\s*- name: (.+?)\s*$", line)
        single = re.match(r"\s*run: (?!\|)(.+?)\s*$", line)
        if named:
            name = named.group(1)
        elif single and name:
            steps[name] = single.group(1)
        elif re.match(r"\s*run: \|\s*$", line) and name:
            indent = len(line) - len(line.lstrip())
            block = []
            for following in lines[number + 1:]:
                if following.strip() and len(following) - len(following.lstrip()) <= indent:
                    break
                block.append(following.strip())
            steps[name] = "\n".join(part for part in block if part)
    return steps


def ci_command(command: str) -> str:
    """A CI command as it runs in the container, where reuse is installed already. Pure."""
    return command.replace("pipx run reuse", "reuse")


def tidy_folders(command: str) -> tuple[str, ...]:
    """The folders a `run-clang-tidy` command with a `(a|b/c)/[^/]+...` pattern looks at. Pure."""
    found = re.search(r"\(([^()]*)\)/\[", command)
    return tuple(part + "/" for part in found.group(1).split("|")) if found else ()


def tidy_key(version: str, config: str, preprocessed: str) -> str:
    """The key of a clang-tidy result: the tool, its configuration and everything the file
    includes. The same key means the same result, so a cached pass is not out of date. Pure."""
    digest = hashlib.sha256()
    for part in (version, config, preprocessed):
        digest.update(part.encode())
        digest.update(b"\0")
    return digest.hexdigest()


def preprocess_command(command: str) -> list[str]:
    """The compile command of a file, turned into one that only preprocesses it. Pure."""
    arguments = shlex.split(command)
    kept: list[str] = []
    skip = 0
    for argument in arguments:
        if skip:
            skip -= 1
        elif argument in ("-o", "-MF", "-MT", "-MQ"):
            skip = 1
        elif argument not in ("-c", "-MD", "-MMD"):
            kept.append(argument)
    return [*kept, "-E", "-P"]


def source_of_object(command: str) -> str | None:
    """The object file a compile command writes. Pure."""
    arguments = shlex.split(command)
    return arguments[arguments.index("-o") + 1] if "-o" in arguments else None


def label(mode: str, steps: list[dict], partial: list[str]) -> str:
    """The sentence that says what was verified. Pure."""
    failed = [step["name"] for step in steps if step["status"] == "failed"]
    if failed:
        return "FAILED: " + ", ".join(failed)
    if mode == "full" and not partial:
        return "FULL VALIDATION PASSED"
    why = "; ".join(partial) if partial else "not a full run"
    return f"TARGETED CHECKS PASSED (not a full validation: {why})"


# ---------------------------------------------------------------- effects

def run(command: list[str], cwd: Path | None = None, quiet: bool = False) -> subprocess.CompletedProcess:
    print("+", " ".join(shlex.quote(part) for part in command), flush=True)
    return subprocess.run(command, cwd=cwd, text=True, capture_output=quiet, check=False)


def git(args: list[str], root: Path) -> str:
    env = {**os.environ, "GIT_OPTIONAL_LOCKS": "0"}
    result = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, env=env, check=False)
    return result.stdout


def worktree_tree(root: Path) -> str:
    """The git tree of the working directory as it is now: the commit plus every change and
    every new file (but not what .gitignore leaves out). It names the exact content that is
    checked, whatever the commits are."""
    with tempfile.TemporaryDirectory() as folder:
        env = {**os.environ, "GIT_INDEX_FILE": str(Path(folder) / "index")}
        for command in (["read-tree", "HEAD"], ["add", "-A", "--", "."]):
            subprocess.run(["git", "-C", str(root), *command], env=env, check=True, capture_output=True)
        written = subprocess.run(["git", "-C", str(root), "write-tree"], env=env, check=True, capture_output=True, text=True)
    return written.stdout.strip()


def tree_id(root: Path) -> str:
    return worktree_tree(root)


def last_full_tree(root: Path) -> str | None:
    """The tree that last passed a full validation, if the repository still has it."""
    path = root / ".check" / "last-full.json"
    if not path.exists():
        return None
    tree = json.loads(path.read_text()).get("tree_id", "")
    known = subprocess.run(["git", "-C", str(root), "cat-file", "-e", tree], capture_output=True, check=False)
    return tree if tree and known.returncode == 0 else None


def check_base(root: Path) -> tuple[str, str]:
    """What the changes are counted from, and its name for the report: the last tree that
    passed in full (everything before it is verified), else where the branch left origin/main."""
    full = last_full_tree(root)
    if full:
        return full, "the last full validation"
    merged = git(["merge-base", "HEAD", "origin/main"], root).strip()
    return (merged, "origin/main") if merged else ("HEAD", "the last commit")


def changed_files(root: Path, base: str) -> list[str]:
    """The files that differ between `base` (a commit or a tree) and the working directory."""
    names = git(["diff", "--name-only", base, worktree_tree(root)], root).split("\n")
    return sorted(name for name in names if name)


class Checks:
    """The steps, run in one environment (the container or this machine)."""

    def __init__(self, root: Path, build: Path, cache: Path, changed: list[str], use_cache: bool) -> None:
        self.root, self.build, self.cache, self.changed, self.use_cache = root, build, cache, changed, use_cache
        self.steps: list[dict] = []
        self.partial: list[str] = []
        self.tidy_stats = {"checked": 0, "cached": 0}

    def step(self, name: str, ok: bool, detail: str = "") -> bool:
        self.steps.append({"name": name, "status": "passed" if ok else "failed", "detail": detail})
        print(f"[{'ok' if ok else 'FAILED'}] {name} {detail}", flush=True)
        return ok

    def configure_and_build(self, tree: str, extra: list[str]) -> bool:
        """Builds one tree; the build tool only does what the file dates call for."""
        directory = self.build / tree
        configure = ["cmake", "-S", str(self.root), "-B", str(directory), "-DGALLEON_WERROR=ON",
                     "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", *extra]
        configured = run(configure, quiet=True)
        if configured.returncode != 0:
            print(configured.stdout, configured.stderr, flush=True)
        ok = configured.returncode == 0 and run(["cmake", "--build", str(directory), "-j", str(os.cpu_count())]).returncode == 0
        return self.step(f"build {tree}", ok)

    def all_tests(self, tree: str) -> set[str]:
        out = run(["ctest", "--test-dir", str(self.build / tree), "-N"], quiet=True).stdout
        return set(re.findall(r"Test +#\d+: (\S+)", out))

    def program_key(self, tree: str, name: str) -> str:
        """What the result of one test depends on: its program, the shared libraries of the
        build tree (the program does not contain them) and, for a test that reads the widgets,
        the widgets."""
        directory = self.build / tree
        parts = [(directory / "tests" / name).read_bytes() if (directory / "tests" / name).is_file() else b""]
        parts += [path.read_bytes() for path in sorted(directory.rglob("*.so*")) if path.is_file()]
        if name in tests_using("GALLEON_SOURCE_WIDGETS", self.root / "tests"):
            parts += [path.read_bytes() for path in sorted((self.root / "widgets").rglob("*")) if path.is_file()]
        return inputs_key(parts)

    def passes_file(self, tree: str) -> Path:
        return self.cache / "tests" / f"{tree}.json"

    def recorded_passes(self, tree: str) -> dict[str, str]:
        path = self.passes_file(tree)
        return json.loads(path.read_text()) if path.exists() else {}

    def stale_tests(self, tree: str) -> tuple[set[str], set[str]]:
        """The tests to run in a tree because nothing recorded says they pass as they are now,
        and all the tests of the tree."""
        every = self.all_tests(tree)
        now = {name: self.program_key(tree, name) for name in every}
        return (stale(now, {} if not self.use_cache else self.recorded_passes(tree)), every)

    def run_tests(self, tree: str, names: set[str] | None) -> bool:
        """Runs the tests (all when `names` is None) and records the ones that passed."""
        directory = self.build / tree
        if names is not None and not names:
            return self.step(f"tests {tree}", True, "(none to run: each passed already as it is now)")
        failed_log = directory / "Testing" / "Temporary" / "LastTestsFailed.log"
        failed_log.unlink(missing_ok=True)
        command = ["ctest", "--test-dir", str(directory), "--output-on-failure"]
        if names is not None:
            command += ["-R", "^(" + "|".join(sorted(names)) + ")$"]
        code = run(command).returncode
        failed = {line.split(":", 1)[1].strip() for line in failed_log.read_text().splitlines() if ":" in line} if failed_log.exists() else set()
        ran = self.all_tests(tree) if names is None else names
        passes = self.recorded_passes(tree)
        passes.update({name: self.program_key(tree, name) for name in ran - failed} if code == 0 or failed else {})
        self.passes_file(tree).parent.mkdir(parents=True, exist_ok=True)
        self.passes_file(tree).write_text(json.dumps(passes))
        return self.step(f"tests {tree}", code == 0, "(all)" if names is None else f"({len(names)} selected)")

    def compile_database(self) -> list[dict]:
        return json.loads((self.build / "gcc" / "compile_commands.json").read_text())

    def tidy_files(self) -> list[str]:
        """Every file of our own folders: a result is reused when its content is the same, so
        looking at all of them costs a preprocessing each, not a clang-tidy run."""
        chosen = set()
        for entry in self.compile_database():
            relative = os.path.relpath(entry["file"], self.root)
            if relative.startswith(TIDY_DIRS) and relative.endswith(".cpp"):
                chosen.add(entry["file"])
        return sorted(chosen)

    def tidy(self, files: list[str]) -> bool:
        version = run(["clang-tidy", "--version"], quiet=True).stdout
        config = (self.root / ".clang-tidy").read_text()
        commands = {entry["file"]: entry for entry in self.compile_database()}
        folder = self.cache / "tidy"
        folder.mkdir(parents=True, exist_ok=True)

        def one(file: str) -> tuple[str, bool, bool]:
            entry = commands[file]
            text = subprocess.run(preprocess_command(entry["command"]), capture_output=True, text=True, check=False, cwd=entry["directory"]).stdout
            marker = folder / tidy_key(version, config, text)
            if self.use_cache and marker.exists():
                return file, True, True
            result = subprocess.run(["clang-tidy", "-p", str(self.build / "gcc"), "--quiet", file], capture_output=True, text=True, check=False)
            ok = result.returncode == 0 and " error:" not in result.stdout
            if ok:
                marker.touch()
            else:
                print(result.stdout[-4000:], flush=True)
            return file, ok, False

        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
            results = list(pool.map(one, files))
        self.tidy_stats = {"checked": len(results), "cached": sum(1 for _, _, cached in results if cached)}
        bad = [os.path.relpath(file, self.root) for file, ok, _ in results if not ok]
        return self.step("clang-tidy", not bad, f"({len(results)} files, {self.tidy_stats['cached']} already verified with the same content and rules)" + (f" failing: {', '.join(bad)}" if bad else ""))

    def clean_copy(self, destination: Path) -> None:
        """The files that would be committed (tracked or new, not ignored), as a fresh checkout
        would have them."""
        names = git(["ls-files", "-co", "--exclude-standard", "-z"], self.root).split("\0")
        for name in filter(None, names):
            source = self.root / name
            if source.is_file():
                (destination / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination / name)

    def replay_ci_steps(self) -> None:
        """qmllint, the tests of this tool and reuse, with the commands of the CI file, one after
        the other in a clean copy: the same way the CI job runs them."""
        steps = parse_steps((self.root / CI_FILE).read_text())
        with tempfile.TemporaryDirectory() as folder:
            copy = Path(folder)
            self.clean_copy(copy)
            for name in CI_STEPS_REPLAYED:
                if name not in steps:
                    self.step(f"CI step '{name}'", False, f"(no longer in {CI_FILE}: update CI_STEPS_REPLAYED)")
                    continue
                done = run(["sh", "-c", ci_command(steps[name])], cwd=copy, quiet=True)
                if done.returncode != 0:
                    print(done.stdout[-3000:], done.stderr[-2000:], flush=True)
                self.step(f"CI step '{name}'", done.returncode == 0)

    def static_checks(self) -> None:
        self.replay_ci_steps()

    def fuzz(self) -> None:
        target = self.build / "asan" / "fuzz" / "fuzz_input_report"
        ok = run([str(target), f"-max_total_time={LONG_FUZZ_SECONDS}"], quiet=True).returncode == 0
        self.step("fuzz fuzz_input_report", ok, f"({LONG_FUZZ_SECONDS} s)")


ASAN_FLAGS = ["-DCMAKE_CXX_COMPILER=clang++", "-DGALLEON_SANITIZE=ON", "-DGALLEON_FUZZ=ON"]


def run_mode(mode: str, checks: Checks) -> tuple[str, str]:
    """Runs the checks of a mode. Returns the mode really run and why it was changed, if it was."""
    scope = classify(checks.changed)
    escalated = ""
    if mode != "full" and scope.kind == "full":
        mode, escalated = "full", "; ".join(scope.reasons[:3])
        print(f"Escalated to a full run: {escalated}", flush=True)
    if mode != "full" and scope.kind in ("none", "docs"):
        checks.partial.append("nothing changed since the last full validation" if scope.kind == "none"
                              else "only documents changed since the last full validation: no build or test was needed")
        checks.static_checks()
        return mode, escalated

    if not checks.configure_and_build("gcc", []):
        return mode, escalated
    if mode == "full":
        checks.run_tests("gcc", None)
        if checks.configure_and_build("asan", ASAN_FLAGS):
            checks.run_tests("asan", None)
            checks.fuzz()
        checks.tidy(checks.tidy_files())
        checks.static_checks()
        return mode, escalated

    chosen, every = checks.stale_tests("gcc")
    if too_many_stale(chosen, every):
        print("Most of the tests are out of date: something shared changed. Running everything.", flush=True)
        checks.steps.clear()
        checks.partial.clear()
        return run_mode("full", checks)[0], "most of the tests were out of date (something shared changed)"
    checks.run_tests("gcc", chosen)
    checks.partial.append(f"{len(chosen)} of {len(every)} tests run (those whose program, library or data changed since they last passed)")
    if mode == "batch" and checks.configure_and_build("asan", ASAN_FLAGS):
        chosen_asan, every_asan = checks.stale_tests("asan")
        checks.run_tests("asan", chosen_asan)
        checks.partial.append(f"{len(chosen_asan)} of {len(every_asan)} tests run under the sanitizers")
    if mode == "quick":
        checks.partial.append("no sanitizer build (use batch)")
    checks.tidy(checks.tidy_files())
    checks.static_checks()
    return mode, escalated


# ---------------------------------------------------------------- the command line

def image_up_to_date(root: Path) -> bool:
    wanted = hashlib.sha256((root / "tools/ci/Dockerfile").read_bytes()).hexdigest()
    out = subprocess.run(["docker", "image", "inspect", IMAGE, "--format", '{{index .Config.Labels "dockerfile.sha256"}}'],
                         capture_output=True, text=True, check=False).stdout.strip()
    return out == wanted


def build_image(root: Path) -> bool:
    wanted = hashlib.sha256((root / "tools/ci/Dockerfile").read_bytes()).hexdigest()
    return run(["docker", "build", "-t", IMAGE, "--label", f"dockerfile.sha256={wanted}", str(root / "tools/ci")]).returncode == 0


def write_report(root: Path, report: dict) -> None:
    folder = root / ".check"
    folder.mkdir(exist_ok=True)
    text = json.dumps(report, indent=2)
    (folder / "last.json").write_text(text)
    if report["label"] == "FULL VALIDATION PASSED":
        (folder / "last-full.json").write_text(text)


def execute(args: argparse.Namespace, root: Path, build: Path, cache: Path) -> tuple[int, dict]:
    """Runs the mode in this environment; returns the exit code and the report."""
    if "CHECK_CHANGED" in os.environ:
        changed, based_on = os.environ["CHECK_CHANGED"].split("\n"), os.environ.get("CHECK_BASED_ON", "")
    else:
        base, based_on = check_base(root)
        changed = changed_files(root, base)
    checks = Checks(root, build, cache, [name for name in changed if name], not args.no_cache)
    mode, escalated = run_mode(args.mode, checks)
    report = {"mode": mode, "asked": args.mode, "escalated_because": escalated, "env": args.env,
              "tree_id": os.environ.get("CHECK_TREE_ID") or tree_id(root), "changes_counted_from": based_on,
              "changed": checks.changed,
              "steps": checks.steps, "tidy": checks.tidy_stats, "label": label(mode, checks.steps, checks.partial)}
    print("\n" + report["label"])
    return (1 if report["label"].startswith("FAILED") else 0), report


def inside(args: argparse.Namespace, root: Path) -> int:
    """In the container: the report goes out on a line of its own for the host to keep."""
    code, report = execute(args, root, Path("/build"), Path("/cache"))
    print("REPORT " + json.dumps(report))
    return code


def on_host(args: argparse.Namespace, root: Path) -> int:
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "galleoncore-check"
    code, report = execute(args, root, root / "build-check", cache)
    write_report(root, report)
    return code


def in_container(args: argparse.Namespace, root: Path) -> int:
    if not image_up_to_date(root) and not build_image(root):
        return 2
    base, based_on = check_base(root)
    changed = changed_files(root, base)
    tree = tree_id(root)
    command = ["docker", "run", "--rm", "--init", "-v", f"{root}:/src:ro", "-v", "galleoncore-ci-build:/build",
               "-v", "galleoncore-ci-cache:/cache", "-e", f"CHECK_CHANGED={chr(10).join(changed)}", "-e", f"CHECK_TREE_ID={tree}", "-e", f"CHECK_BASED_ON={based_on}",
               IMAGE, "python3", "/src/tools/check.py", args.mode, "--env", "inside"]
    if args.no_cache:
        command.append("--no-cache")
    print("+", " ".join(shlex.quote(part) for part in command[:8]), "...", flush=True)
    process = subprocess.Popen(command, stdout=subprocess.PIPE, text=True)
    report = None
    for line in process.stdout or []:
        if line.startswith("REPORT "):
            report = json.loads(line[len("REPORT "):])
        else:
            print(line, end="", flush=True)
    code = process.wait()
    if report:
        report["env"] = "docker"
        write_report(root, report)
    return code


def status(root: Path) -> int:
    path = root / ".check" / "last-full.json"
    if not path.exists():
        print("No full validation recorded: run `tools/check.py full`.")
        return 1
    report = json.loads(path.read_text())
    same = report["tree_id"] == tree_id(root)
    print(f"Last full validation: {'this exact tree' if same else 'an OTHER tree (code changed since)'} ({report['env']}, tree {report['tree_id']}).")
    return 0 if same else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Targeted and full checks of GalleonCore.")
    parser.add_argument("mode", choices=["quick", "batch", "full", "status", "image"])
    parser.add_argument("--env", choices=["docker", "host", "inside"], default="docker")
    parser.add_argument("--no-cache", action="store_true", help="do not reuse earlier clang-tidy results")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if args.mode == "status":
        return status(root)
    if args.mode == "image":
        return 0 if build_image(root) else 2
    if args.env == "docker":
        return in_container(args, root)
    if args.env == "inside":
        return inside(args, root)
    return on_host(args, root)


if __name__ == "__main__":
    sys.exit(main())
