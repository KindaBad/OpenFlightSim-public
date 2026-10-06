#!/usr/bin/env bash
# Open the OpenFlightSim launcher from this checkout. First use prepares everything.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$project_dir/scripts/play.py" "$@"
