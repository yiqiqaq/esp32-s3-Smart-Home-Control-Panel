#!/usr/bin/env bash
# Load the ESP-IDF environment for this project (ESP-IDF 5.5.5 by default).
# Usage from the repository root: source os/export_idf.sh

if [[ -n "${ZSH_VERSION:-}" ]]; then
    _script_path="${(%):-%N}"
else
    _script_path="${BASH_SOURCE[0]}"
fi
_project_dir="$(cd "$(dirname "${_script_path}")" && pwd)"
_idf_default="${HOME}/.espressif/v5.5.5/esp-idf"
_idf_path="${IDF_PATH:-${_idf_default}}"

if [[ ! -f "${_idf_path}/export.sh" ]]; then
    echo "ESP-IDF not found at ${_idf_path}. Set IDF_PATH to an installed ESP-IDF directory." >&2
    return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1091
source "${_idf_path}/export.sh"
export IDF_TARGET=esp32s3
cd "${_project_dir}"
unset _project_dir _idf_default _idf_path _script_path
