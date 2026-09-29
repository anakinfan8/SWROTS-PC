"""Repository hygiene check: keeps game content and personal data out of the repository.

    python tools/check_repo.py [--commits <range>]

Checks every tracked file (or, with --staged, the files staged for commit):
  - no game or binary files (disc images, archives, executables, the game's asset formats),
  - nothing unusually large (third-party sources excepted),
  - no email addresses except GitHub no-reply addresses,
  - no local user paths (C:\\Users\\<name>, /home/<name>),
  - none of the words listed in the HYGIENE_WORDS environment variable (comma separated), for
    names that must not appear but should not be written into the repository either.
With --commits <range> (e.g. origin/main..HEAD, or --all), also checks each commit's author and
committer email and message.

Exit code 1 and a list of findings if anything is wrong.
"""
import os
import re
import subprocess
import sys

MAX_BYTES = 1 << 20
LARGE_OK = ("third_party/", "LICENSE")
BINARY_EXTENSIONS = {
    # disc images and archives
    ".iso", ".xiso", ".7z", ".zip", ".rar", ".cso", ".bin", ".img",
    # executables and build output
    ".xbe", ".exe", ".dll", ".lib", ".obj", ".pdb", ".exp", ".ilk", ".so", ".dylib",
    # the game's formats
    ".pak", ".pk2", ".stx", ".msh", ".bnm", ".ban", ".hwx", ".wxb", ".wma", ".xwb", ".sfd", ".rpx",
    ".xbx", ".tag", ".dds", ".tga", ".bik", ".wav", ".mp3", ".ogg",
    # images (none are expected; project art would be reviewed by hand)
    ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".ico", ".psd",
}
EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")
NOREPLY = re.compile(r"^[0-9]+\+[A-Za-z0-9-]+@users\.noreply\.github\.com$|^noreply@github\.com$"
                     r"|^[0-9]+\+github-actions\[bot\]@users\.noreply\.github\.com$")
# Windows profile paths (any case), and Linux home paths (always lower case).
USER_PATH = re.compile(r"(?i:[a-z]:[\\/]+users[\\/]+(?!public\b|<|%|\*)[^\\/\s\"'<>]+)|/home/[a-z][a-z0-9_-]*/")
TEXT_SKIP_EMAIL = ("LICENSE", "third_party/")


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8",
                          errors="replace", check=True).stdout


def words():
    raw = os.environ.get("HYGIENE_WORDS", "")
    return [w.strip() for w in raw.split(",") if w.strip()]


def check_text(where, text, findings, skip_email=False):
    for w in words():
        if re.search(re.escape(w), text, re.IGNORECASE):
            findings.append(f"{where}: contains a word from HYGIENE_WORDS")
    if not skip_email:
        for m in EMAIL.finditer(text):
            if not NOREPLY.match(m.group(0)):
                findings.append(f"{where}: email address {m.group(0)}")
    for m in USER_PATH.finditer(text):
        findings.append(f"{where}: local user path {m.group(0)}")


def check_files(staged):
    findings = []
    if staged:
        names = [n for n in git("diff", "--cached", "--name-only", "--diff-filter=ACMR").splitlines() if n]
    else:
        names = [n for n in git("ls-files").splitlines() if n]
    for name in names:
        ext = os.path.splitext(name)[1].lower()
        if ext in BINARY_EXTENSIONS:
            findings.append(f"{name}: file type {ext} is not allowed in the repository")
            continue
        data = git("show", f":{name}") if staged else open(name, encoding="utf-8", errors="replace").read()
        size = len(data.encode("utf-8", errors="replace"))
        if size > MAX_BYTES and not name.startswith(LARGE_OK):
            findings.append(f"{name}: {size} bytes (larger than {MAX_BYTES})")
        if "\0" in data:
            findings.append(f"{name}: binary content")
            continue
        check_text(name, data, findings, skip_email=name.startswith(TEXT_SKIP_EMAIL))
        # the file name itself
        check_text(f"{name} (name)", name, findings)
    return findings


def check_commits(rng):
    findings = []
    args = ["log", "--format=%H%x1f%an%x1f%ae%x1f%cn%x1f%ce%x1f%B%x1e"]
    args += ["--all"] if rng == "--all" else [rng]
    for record in git(*args).split("\x1e"):
        fields = record.strip("\n").split("\x1f")
        if len(fields) < 6:
            continue
        sha, an, ae, cn, ce, body = fields[:6]
        short = sha[:8]
        for label, email in (("author", ae), ("committer", ce)):
            if not NOREPLY.match(email):
                findings.append(f"commit {short}: {label} email {email} is not a GitHub no-reply address")
        check_text(f"commit {short} message", body, findings)
        check_text(f"commit {short} names", f"{an} {cn}", findings, skip_email=True)
    return findings


def main():
    staged = "--staged" in sys.argv
    findings = check_files(staged)
    if "--commits" in sys.argv:
        i = sys.argv.index("--commits")
        findings += check_commits(sys.argv[i + 1] if i + 1 < len(sys.argv) else "HEAD")
    if findings:
        print("Repository hygiene check failed:")
        for f in findings:
            print("  " + f)
        return 1
    print("Repository hygiene check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
