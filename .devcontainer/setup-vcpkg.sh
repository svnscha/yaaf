#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vcpkg_dir="${repo_root}/vcpkg"

if [[ -r /etc/os-release ]]; then
  . /etc/os-release
  if [[ "${ID:-}" == "alpine" ]]; then
    export VCPKG_FORCE_SYSTEM_BINARIES=1
  fi
fi

if [[ ! -f "${repo_root}/.gitmodules" ]]; then
  printf 'Missing .gitmodules in %s\n' "${repo_root}" >&2
  exit 1
fi

if [[ ! -e "${vcpkg_dir}/.git" ]]; then
  printf 'Missing initialized vcpkg submodule at %s\n' "${vcpkg_dir}" >&2
  printf 'Run git submodule update --init --recursive before bootstrapping vcpkg.\n' >&2
  exit 1
fi

if [[ ! -x "${vcpkg_dir}/bootstrap-vcpkg.sh" ]]; then
  printf 'Missing vcpkg bootstrap script at %s\n' "${vcpkg_dir}/bootstrap-vcpkg.sh" >&2
  exit 1
fi

"${vcpkg_dir}/bootstrap-vcpkg.sh"
