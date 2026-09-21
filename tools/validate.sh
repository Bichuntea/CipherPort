#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_password_manager_model.c main/password_manager_model.c \
        -o "${test_dir}/test_password_manager_model"
    "${test_dir}/test_password_manager_model"
    python3 tests/test_verify_firmware.py
    python3 tests/test_flash_package.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_root
    local factory_build_dir
    local legacy_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_root="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_root}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_root}" ;; esac' EXIT
    factory_build_dir="${validation_root}/factory-4m"
    legacy_build_dir="${validation_root}/legacy-3m"

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${factory_build_dir}" \
        -D "SDKCONFIG=${factory_build_dir}/sdkconfig" build
    idf.py -B "${factory_build_dir}" merge-bin \
        -o "${factory_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${factory_build_dir}" --profile factory-4m

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults;${repo_root}/sdkconfig.legacy.defaults" \
        idf.py -B "${legacy_build_dir}" \
        -D "SDKCONFIG=${legacy_build_dir}/sdkconfig" build
    idf.py -B "${legacy_build_dir}" merge-bin \
        -o "${legacy_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${legacy_build_dir}" --profile legacy-3m

    mkdir -p "${repo_root}/build"
    python3 tools/package_firmware.py "${factory_build_dir}" \
        "${legacy_build_dir}" "${repo_root}/build"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
