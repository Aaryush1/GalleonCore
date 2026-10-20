#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

"""Tests of the decisions of tools/check.py: what a change calls for, which tests and which
files are looked at, when a cached result may be reused, and what a report may claim.
Run with: python3 -m unittest discover -s tools -p 'test_*.py'"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check  # noqa: E402


class Classify(unittest.TestCase):
    def kind(self, *paths: str) -> str:
        return check.classify(list(paths)).kind

    def test_nothing_changed(self) -> None:
        self.assertEqual(self.kind(), "none")

    def test_documents_only(self) -> None:
        self.assertEqual(self.kind("README.md", "docs/ROADMAP.md", "ROADMAP.md"), "docs")
        self.assertEqual(self.kind("tools/systemd/galleond.service"), "docs")

    def test_built_code_is_targeted(self) -> None:
        self.assertEqual(self.kind("daemon/PagesController.cpp"), "targeted")
        self.assertEqual(self.kind("bosun/qml/Bosun.qml", "tests/test_bosun.cpp"), "targeted")
        self.assertEqual(self.kind("widgets/NowPlaying.qml"), "targeted")

    def test_documents_do_not_widen_a_targeted_run(self) -> None:
        self.assertEqual(self.kind("daemon/PageOrder.cpp", "ROADMAP.md"), "targeted")

    def test_shared_things_run_in_full(self) -> None:
        for path in ("CMakeLists.txt", "bosun/CMakeLists.txt", ".clang-tidy", ".github/workflows/ci.yml",
                     "tools/ci/Dockerfile", "tools/check.py", "tests/lsan.supp", "REUSE.toml",
                     "lib/src/Protocol.cpp", "lib/include/galleon/Protocol.h", "fuzz/fuzz_input_report.cpp",
                     "data/devices/galleon-100-sd.json", "public/wallpaper/x.jpg"):
            with self.subTest(path=path):
                scope = check.classify(["daemon/PageOrder.cpp", path])
                self.assertEqual(scope.kind, "full")
                self.assertTrue(any(path in reason for reason in scope.reasons))

    def test_an_unknown_kind_of_file_is_not_guessed(self) -> None:
        scope = check.classify(["something/new.xyz"])
        self.assertEqual(scope.kind, "full")
        self.assertIn("not a kind of file", scope.reasons[0])


class Selection(unittest.TestCase):
    def test_a_test_is_stale_until_a_pass_is_recorded_for_what_it_is_now(self) -> None:
        now = {"test_a": "k1", "test_b": "k2", "test_c": "k3"}
        self.assertEqual(check.stale(now, {}), {"test_a", "test_b", "test_c"})
        self.assertEqual(check.stale(now, {"test_a": "k1", "test_b": "OLD"}), {"test_b", "test_c"})
        self.assertEqual(check.stale(now, now), set())

    def test_the_key_follows_every_input(self) -> None:
        key = check.inputs_key([b"program", b"library"])
        self.assertEqual(key, check.inputs_key([b"program", b"library"]))
        self.assertNotEqual(key, check.inputs_key([b"program", b"library2"]))
        self.assertNotEqual(key, check.inputs_key([b"program2", b"library"]))
        self.assertNotEqual(key, check.inputs_key([b"programlibrary"]))

    def test_most_tests_stale_means_a_shared_change(self) -> None:
        names = {f"test_{n}" for n in range(10)}
        self.assertFalse(check.too_many_stale({f"test_{n}" for n in range(6)}, names))
        self.assertTrue(check.too_many_stale({f"test_{n}" for n in range(7)}, names))
        self.assertFalse(check.too_many_stale(set(), set()))

    def test_tests_that_read_a_data_folder_are_found(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            tests = Path(folder)
            (tests / "test_screen.cpp").write_text("const char *d = GALLEON_SOURCE_WIDGETS;")
            (tests / "test_other.cpp").write_text("int x;")
            self.assertEqual(check.tests_using("GALLEON_SOURCE_WIDGETS", tests), {"test_screen"})


class TidyCache(unittest.TestCase):
    def test_the_key_follows_everything_the_result_depends_on(self) -> None:
        key = check.tidy_key("clang-tidy 18", "Checks: a", "int x;")
        self.assertEqual(key, check.tidy_key("clang-tidy 18", "Checks: a", "int x;"))
        self.assertNotEqual(key, check.tidy_key("clang-tidy 21", "Checks: a", "int x;"))
        self.assertNotEqual(key, check.tidy_key("clang-tidy 18", "Checks: a, b", "int x;"))
        self.assertNotEqual(key, check.tidy_key("clang-tidy 18", "Checks: a", "int y;"))

    def test_parts_cannot_be_shifted_into_each_other(self) -> None:
        self.assertNotEqual(check.tidy_key("ab", "c", "d"), check.tidy_key("a", "bc", "d"))

    def test_the_compile_command_becomes_a_preprocessing_command(self) -> None:
        command = "/usr/bin/c++ -DX=1 -I/a\\ b/inc -isystem /q -Wall -MD -MT x.o -MF x.o.d -o x.o -c /src/f.cpp"
        self.assertEqual(check.preprocess_command(command),
                         ["/usr/bin/c++", "-DX=1", "-I/a b/inc", "-isystem", "/q", "-Wall", "/src/f.cpp", "-E", "-P"])

    def test_the_object_file_is_found(self) -> None:
        self.assertEqual(check.source_of_object("c++ -c f.cpp -o dir/f.cpp.o"), "dir/f.cpp.o")
        self.assertIsNone(check.source_of_object("c++ -E f.cpp"))


class Report(unittest.TestCase):
    ok = [{"name": "tests", "status": "passed"}]

    def test_only_a_complete_full_run_says_full(self) -> None:
        self.assertEqual(check.label("full", self.ok, []), "FULL VALIDATION PASSED")

    def test_a_targeted_run_says_what_it_did_not_cover(self) -> None:
        text = check.label("quick", self.ok, ["2 test(s) run"])
        self.assertTrue(text.startswith("TARGETED CHECKS PASSED"))
        self.assertIn("not a full validation", text)
        self.assertIn("2 test(s) run", text)

    def test_a_full_run_with_a_gap_is_not_called_full(self) -> None:
        self.assertTrue(check.label("full", self.ok, ["no fuzzing"]).startswith("TARGETED"))

    def test_a_failure_names_its_step(self) -> None:
        steps = [*self.ok, {"name": "clang-tidy", "status": "failed"}]
        self.assertEqual(check.label("full", steps, []), "FAILED: clang-tidy")


class CiFile(unittest.TestCase):
    sample = """jobs:
  lint:
    steps:
      - name: Install
        run: |
          sudo apt-get update
          sudo apt-get install -y a b
      - name: qmllint
        run: /usr/lib/qt6/bin/qmllint widgets/*.qml
      - name: REUSE licensing
        run: pipx run reuse lint
      - uses: actions/checkout@v4
      - name: Without a command
"""

    def test_the_commands_of_the_named_steps_are_read(self) -> None:
        steps = check.parse_steps(self.sample)
        self.assertEqual(steps["qmllint"], "/usr/lib/qt6/bin/qmllint widgets/*.qml")
        self.assertEqual(steps["REUSE licensing"], "pipx run reuse lint")
        self.assertEqual(steps["Install"], "sudo apt-get update\nsudo apt-get install -y a b")
        self.assertNotIn("Without a command", steps)

    def test_reuse_runs_from_the_container_not_through_pipx(self) -> None:
        self.assertEqual(check.ci_command("pipx run reuse lint"), "reuse lint")
        self.assertEqual(check.ci_command("qmllint x.qml"), "qmllint x.qml")

    def test_the_folders_of_the_lint_pattern_are_read(self) -> None:
        command = "run-clang-tidy -quiet -p build '(lib/src|daemon|bosun|tests)/[^/]+\\.cpp$'"
        self.assertEqual(check.tidy_folders(command), ("lib/src/", "daemon/", "bosun/", "tests/"))
        self.assertEqual(check.tidy_folders("echo nothing"), ())

    def test_the_replayed_steps_and_the_lint_folders_still_exist_in_the_ci_file(self) -> None:
        """The CI file is the one source: if a step is renamed or the folders change, this fails
        and says so, rather than the checks quietly drifting from the CI."""
        root = Path(__file__).resolve().parent.parent
        steps = check.parse_steps((root / check.CI_FILE).read_text())
        for name in check.CI_STEPS_REPLAYED:
            self.assertIn(name, steps)
        self.assertEqual(check.tidy_folders(steps["clang-tidy"]), check.TIDY_DIRS)


class CleanCopy(unittest.TestCase):
    def test_the_copy_has_what_would_be_committed_and_not_what_is_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / "repo"
            root.mkdir()
            for command in (["init", "-q"], ["config", "user.email", "t@example.org"], ["config", "user.name", "t"]):
                subprocess.run(["git", "-C", str(root), *command], check=True)
            (root / ".gitignore").write_text("build/\n__pycache__/\n")
            (root / "tracked.txt").write_text("a")
            (root / "gone.txt").write_text("b")
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(["git", "-C", str(root), "commit", "-qm", "x"], check=True)
            (root / "gone.txt").unlink()                      # tracked, deleted
            (root / "new.txt").write_text("n")                # new
            (root / "build").mkdir()
            (root / "build" / "junk.o").write_text("o")       # ignored
            (root / "sub").mkdir()
            (root / "sub" / "deep.txt").write_text("d")
            checks = check.Checks(root, root / "b", root / "c", [], True)
            copy = Path(folder) / "copy"
            copy.mkdir()
            checks.clean_copy(copy)
            found = sorted(str(path.relative_to(copy)) for path in copy.rglob("*") if path.is_file())
            self.assertEqual(found, [".gitignore", "new.txt", "sub/deep.txt", "tracked.txt"])


class Git(unittest.TestCase):
    def setUp(self) -> None:
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        self.addCleanup(self.folder.cleanup)
        for command in (["init", "-q"], ["config", "user.email", "t@example.org"], ["config", "user.name", "t"]):
            subprocess.run(["git", "-C", str(self.root), *command], check=True)
        (self.root / ".gitignore").write_text("/.check/\n")
        (self.root / "a.txt").write_text("one")
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.root), "commit", "-qm", "first"], check=True)

    def test_the_tree_has_a_stable_name_that_follows_any_change(self) -> None:
        first = check.tree_id(self.root)
        self.assertEqual(first, check.tree_id(self.root))
        (self.root / "a.txt").write_text("two")
        edited = check.tree_id(self.root)
        self.assertNotEqual(first, edited)
        (self.root / "new.txt").write_text("x")
        added = check.tree_id(self.root)
        self.assertNotEqual(edited, added)
        (self.root / "new.txt").write_text("y")
        self.assertNotEqual(added, check.tree_id(self.root))

    def test_what_the_checks_write_is_not_part_of_the_tree(self) -> None:
        first = check.tree_id(self.root)
        (self.root / ".check").mkdir()
        (self.root / ".check" / "last.json").write_text("{}")
        self.assertEqual(first, check.tree_id(self.root))

    def test_changed_files_are_the_edited_and_the_new_ones(self) -> None:
        (self.root / "a.txt").write_text("two")
        (self.root / "new.txt").write_text("x")
        self.assertEqual(check.changed_files(self.root, "HEAD"), ["a.txt", "new.txt"])

    def test_changes_are_counted_from_the_base_that_is_given(self) -> None:
        (self.root / "a.txt").write_text("two")
        base = check.worktree_tree(self.root)
        self.assertEqual(check.changed_files(self.root, base), [])
        (self.root / "b.txt").write_text("y")
        (self.root / "a.txt").unlink()
        self.assertEqual(check.changed_files(self.root, base), ["a.txt", "b.txt"])

    def test_the_last_full_validation_is_the_base_when_there_is_one(self) -> None:
        self.assertIsNone(check.last_full_tree(self.root))
        self.assertEqual(check.check_base(self.root), ("HEAD", "the last commit"))
        tree = check.worktree_tree(self.root)
        (self.root / ".check").mkdir()
        (self.root / ".check" / "last-full.json").write_text(json.dumps({"tree_id": tree}))
        self.assertEqual(check.check_base(self.root), (tree, "the last full validation"))
        # A tree that the repository does not have cannot be a base.
        (self.root / ".check" / "last-full.json").write_text(json.dumps({"tree_id": "0" * 40}))
        self.assertIsNone(check.last_full_tree(self.root))


class Trees(unittest.TestCase):
    """A fake source tree and build tree, to see what the checks look at."""

    def make(self, changed: list[str] | None = None) -> check.Checks:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        root = Path(folder.name) / "src"
        build = Path(folder.name) / "build"
        (root / "tests").mkdir(parents=True)
        (root / "widgets").mkdir()
        (build / "gcc" / "tests").mkdir(parents=True)
        (root / "tests" / "test_screen.cpp").write_text("GALLEON_SOURCE_WIDGETS")
        (root / "tests" / "test_plain.cpp").write_text("int main();")
        (root / "widgets" / "Clock.qml").write_text("one")
        (build / "gcc" / "tests" / "test_screen").write_bytes(b"screen program")
        (build / "gcc" / "tests" / "test_plain").write_bytes(b"plain program")
        (build / "gcc" / "libgalleon.so").write_bytes(b"library")
        entries = []
        for name in ("daemon/A.cpp", "daemon/B.cpp", "tests/test_plain.cpp", "fuzz/f.cpp"):
            entries.append({"file": str(root / name), "directory": str(build / "gcc"),
                            "command": f"c++ -c {root / name} -o CMakeFiles/x.dir/{Path(name).name}.o"})
        entries.append({"file": str(build / "gcc/x_autogen/mocs.cpp"), "directory": str(build / "gcc"),
                        "command": "c++ -c mocs.cpp -o mocs.o"})
        (build / "gcc" / "compile_commands.json").write_text(json.dumps(entries))
        self.root, self.build = root, build
        return check.Checks(root, build, Path(folder.name) / "cache", changed or [], True)

    def test_clang_tidy_looks_at_every_file_of_our_folders(self) -> None:
        checks = self.make()
        files = [str(Path(file).relative_to(self.root)) for file in checks.tidy_files()]
        self.assertEqual(sorted(files), ["daemon/A.cpp", "daemon/B.cpp", "tests/test_plain.cpp"])

    def test_the_key_of_a_test_follows_its_program_the_libraries_and_its_data(self) -> None:
        checks = self.make()
        screen, plain = checks.program_key("gcc", "test_screen"), checks.program_key("gcc", "test_plain")
        (self.build / "gcc" / "tests" / "test_plain").write_bytes(b"plain program, changed")
        self.assertNotEqual(plain, checks.program_key("gcc", "test_plain"))
        self.assertEqual(screen, checks.program_key("gcc", "test_screen"))
        (self.build / "gcc" / "libgalleon.so").write_bytes(b"library, changed")
        self.assertNotEqual(screen, checks.program_key("gcc", "test_screen"))
        screen = checks.program_key("gcc", "test_screen")
        plain = checks.program_key("gcc", "test_plain")
        (self.root / "widgets" / "Clock.qml").write_text("two")
        self.assertNotEqual(screen, checks.program_key("gcc", "test_screen"))  # reads the widgets
        self.assertEqual(plain, checks.program_key("gcc", "test_plain"))        # does not

    def test_a_test_without_a_program_does_not_share_the_key_of_one_that_has_it(self) -> None:
        checks = self.make()
        self.assertNotEqual(checks.program_key("gcc", "test_missing"), checks.program_key("gcc", "test_plain"))

    def test_recorded_passes_decide_what_is_stale(self) -> None:
        checks = self.make()
        checks.all_tests = lambda tree: {"test_screen", "test_plain"}  # type: ignore[method-assign]
        chosen, every = checks.stale_tests("gcc")
        self.assertEqual((chosen, every), ({"test_screen", "test_plain"}, {"test_screen", "test_plain"}))
        checks.passes_file("gcc").parent.mkdir(parents=True)
        checks.passes_file("gcc").write_text(json.dumps({"test_plain": checks.program_key("gcc", "test_plain"),
                                                         "test_screen": "an old key"}))
        self.assertEqual(checks.stale_tests("gcc")[0], {"test_screen"})
        checks.use_cache = False
        self.assertEqual(checks.stale_tests("gcc")[0], {"test_screen", "test_plain"})


if __name__ == "__main__":
    unittest.main()
