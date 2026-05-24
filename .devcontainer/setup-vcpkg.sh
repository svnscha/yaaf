#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vcpkg_dir="${repo_root}/vcpkg"
vcpkg_ref="e5a4f54c0d562059e9ccc6f7e7150667da58fe41"
vcpkg_repo="https://github.com/microsoft/vcpkg.git"

if [[ -r /etc/os-release ]]; then
  . /etc/os-release
  if [[ "${ID:-}" == "alpine" ]]; then
    export VCPKG_FORCE_SYSTEM_BINARIES=1
  fi
fi

if [[ ! -d "${vcpkg_dir}/.git" ]]; then
  rm -rf "${vcpkg_dir}"
  git init "${vcpkg_dir}" >/dev/null
  git -C "${vcpkg_dir}" remote add origin "${vcpkg_repo}"
fi

git -C "${vcpkg_dir}" fetch --depth 1 origin "${vcpkg_ref}"
git -C "${vcpkg_dir}" checkout --force --detach "${vcpkg_ref}"

"${vcpkg_dir}/bootstrap-vcpkg.sh"