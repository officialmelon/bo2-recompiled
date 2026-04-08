#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

PRESET="linux-amd64-relwithdebinfo"
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
      --preset)
        shift
        [[ $# -gt 0 ]] || { echo "ERROR: --preset requires a value." >&2; exit 1; }
        PRESET="$1"
        ;;
      *)
        if [[ "$1" =~ ^[a-z][a-z0-9-]*-[a-z0-9][a-z0-9-]*-[a-z0-9][a-z0-9-]*$ ]]; then
          PRESET="$1"
        else
          echo "ERROR: Unknown argument '$1'." >&2
          exit 1
        fi
        ;;
    esac
    shift
  done
}

configure_target() {
  local project="$1"
  echo "Configuring ${project} with preset ${PRESET}..."
  cd "${ROOT}/${project}"

  local cmd=(cmake --preset "${PRESET}")
  if [[ -n "${REXSDK_DIR:-}" ]]; then
    cmd+=(-DREXSDK_DIR="${REXSDK_DIR}")
  fi

  "${cmd[@]}"
}

parse_args "$@"

if [[ "${TARGET_DEFAULT}" == "0" && "${TARGET_MP}" == "0" ]]; then
  TARGET_DEFAULT=1
fi

if [[ "${TARGET_DEFAULT}" == "1" ]]; then
  configure_target default
fi

if [[ "${TARGET_MP}" == "1" ]]; then
  configure_target default_mp
fi

echo "Preset setup complete."
