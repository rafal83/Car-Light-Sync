Import("env")
import subprocess
from pathlib import Path
from datetime import datetime, timedelta, timezone


import os

def get_version_string():
    # Try to get the current tag
    try:
        tag = subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match"],
            stderr=subprocess.STDOUT,
            encoding="utf-8",
        ).strip()
        if tag:
            return tag
    except Exception:
        pass

    try:
        now = datetime.now(timezone.utc)
        iso_weekday = now.isoweekday()  # Monday=1
        start_of_week = (now - timedelta(days=iso_weekday - 1)).replace(
            hour=0, minute=0, second=0, microsecond=0
        )

        weekly_count = subprocess.check_output(
            [
                "git",
                "rev-list",
                "--count",
                f"--since={start_of_week.isoformat()}",
                "HEAD",
            ],
            stderr=subprocess.STDOUT,
            encoding="utf-8",
        ).strip()
    except Exception:
        weekly_count = "0"

    today = datetime.now(timezone.utc).isocalendar()
    year = today[0]
    week = today[1]

    version = f"{year}.{week:02d}.{int(weekly_count):d}"

    return version


print(f"[version] Retrieve version")
git_version = get_version_string()
print(f"[version] Firmware version: {git_version}")

# Export to GitHub Actions if running in CI
if "GITHUB_OUTPUT" in os.environ:
    with open(os.environ["GITHUB_OUTPUT"], "a") as f:
        f.write(f"version={git_version}\n")

if "GITHUB_ENV" in os.environ:
    with open(os.environ["GITHUB_ENV"], "a") as f:
        f.write(f"APP_VERSION={git_version}\n")

project_dir = Path(env['PROJECT_DIR'])
header_path = project_dir / "include" / "version_auto.h"
header_path.write_text(
    f'#ifndef APP_GIT_VERSION\n#define APP_GIT_VERSION "{git_version}"\n#endif\n',
    encoding="utf-8"
)
