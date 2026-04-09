"""Pre-build script: ensures git hooks path is configured."""
import subprocess

try:
    result = subprocess.run(
        ["git", "config", "core.hooksPath"],
        capture_output=True, text=True
    )
    if result.stdout.strip() != ".githooks":
        subprocess.run(["git", "config", "core.hooksPath", ".githooks"], check=True)
        print("Configured git hooks path -> .githooks")
except Exception:
    pass  # Not a git repo or git not installed — skip silently
