Import("env")
import subprocess
from pathlib import Path
from datetime import datetime


def get_version_string():
    try:
        commit_count = subprocess.check_output(
            ["git", "rev-list", "--count", "HEAD"],
            stderr=subprocess.STDOUT,
            encoding="utf-8"
        ).strip()
    except Exception:
        commit_count = "0"

    today = datetime.utcnow().isocalendar()
    year = today[0]
    week = today[1]

    version = f"{year}.{week:02d}.{int(commit_count):d}"

    return version


git_version = get_version_string()
print(f"[version] Firmware version: {git_version}")

project_dir = Path(env['PROJECT_DIR'])
header_path = project_dir / "include" / "version_auto.h"
header_path.write_text(
    f'#ifndef APP_GIT_VERSION\n#define APP_GIT_VERSION "{git_version}"\n#endif\n',
    encoding="utf-8"
)
