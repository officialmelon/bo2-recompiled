#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

TARGET_DEFAULT=0
TARGET_MP=0

parse_args() {
  while (($#)); do
    case "$1" in
      sp|default)
        TARGET_DEFAULT=1
        ;;
      mp|default_mp)
        TARGET_MP=1
        ;;
      both)
        TARGET_DEFAULT=1
        TARGET_MP=1
        ;;
      *)
        echo "ERROR: Unknown argument '$1'." >&2
        exit 1
        ;;
    esac
    shift
  done
}

run_codegen() {
  local project="$1"
  local config_file="$2"
  echo "Running codegen for ${project}..."
  cd "${ROOT}/${project}"
  rexglue codegen "${config_file}"
}

parse_args "$@"

if [[ "${TARGET_DEFAULT}" == "0" && "${TARGET_MP}" == "0" ]]; then
  TARGET_DEFAULT=1
fi

if [[ "${TARGET_DEFAULT}" == "1" ]]; then
  run_codegen default default_config.toml
fi

if [[ "${TARGET_MP}" == "1" ]]; then
  run_codegen default_mp default_mp_config.toml
fi

echo "Done."
