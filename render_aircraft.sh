#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export OCIO="$project_dir/output/color_management/config.ocio"
exec blender -b "$project_dir/output/Airbus_A320.blend" -t 10 --python "$project_dir/scripts/finalize_and_render.py"
