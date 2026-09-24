#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the OpenColorIO Project.

TLRENDER_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

# For OS X
export DYLD_LIBRARY_PATH="${TLRENDER_ROOT}/lib:${DYLD_LIBRARY_PATH}"

# For Linux
export LD_LIBRARY_PATH="${TLRENDER_ROOT}/lib:${LD_LIBRARY_PATH}"

export PATH="${TLRENDER_ROOT}/bin:${PATH}"
export PYTHONPATH="${TLRENDER_ROOT}/:${PYTHONPATH}"
