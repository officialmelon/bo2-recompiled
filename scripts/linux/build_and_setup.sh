#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
OUT="${ROOT}/out"
mkdir -p "${OUT}"

PRESET="linux-amd64-relwithdebinfo"
TARGET_DEFAULT=0
TARGET_MP=0
BUILD_TARGET_ARGS=()

parse_args() {
  while (($#)); do
    case "$1" in
      sp|default)
        TARGET_DEFAULT=1
        BUILD_TARGET_ARGS+=("sp")
        ;;
      mp|default_mp)
        TARGET_MP=1
        BUILD_TARGET_ARGS+=("mp")
        ;;
      both)
        TARGET_DEFAULT=1
        TARGET_MP=1
        BUILD_TARGET_ARGS=("both")
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

build_target() {
  local project="$1"
  local output_name="$2"
  local built_exe="${ROOT}/${project}/out/build/${PRESET}/${output_name}"

  echo "Building ${project}..."
  cd "${ROOT}/${project}"
  cmake --build --preset "${PRESET}"

  if [[ ! -f "${built_exe}" ]]; then
    echo "ERROR: Built executable not found:" >&2
    echo "${built_exe}" >&2
    exit 1
  fi

  cp -f "${built_exe}" "${OUT}/${output_name}"
  echo "Done building ${project}"
  echo
}

parse_args "$@"

if [[ "${TARGET_DEFAULT}" == "0" && "${TARGET_MP}" == "0" ]]; then
  TARGET_DEFAULT=1
  BUILD_TARGET_ARGS=("sp")
fi

echo "Using preset: ${PRESET}"
echo

"${SCRIPT_DIR}/preset.sh" "${BUILD_TARGET_ARGS[@]}" --preset "${PRESET}"

if [[ "${TARGET_DEFAULT}" == "1" ]]; then
  build_target default default
fi

if [[ "${TARGET_MP}" == "1" ]]; then
  build_target default_mp default_mp
fi

echo "All tasks completed."
