#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$project_root"

python_bin="$project_root/.venv-ragas/bin/python"
if [[ ! -x "$python_bin" ]]; then
  echo "missing .venv-ragas; create the virtual environment first" >&2
  exit 2
fi

config_args=()
if [[ -n "${RAGAS_CONFIG:-}" ]]; then
  config_args=(--config "$RAGAS_CONFIG")
fi

"$python_bin" evaluation/ragas/collect_responses.py "${config_args[@]}" "$@"
"$python_bin" evaluation/ragas/evaluate.py "${config_args[@]}"
