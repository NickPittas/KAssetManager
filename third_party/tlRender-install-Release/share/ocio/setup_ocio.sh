#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the OpenColorIO Project.

# For OS X
export DYLD_LIBRARY_PATH="/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/lib:${DYLD_LIBRARY_PATH}"

# For Linux
export LD_LIBRARY_PATH="/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/lib:${LD_LIBRARY_PATH}"

export PATH="/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/bin:${PATH}"
export PYTHONPATH="/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/:${PYTHONPATH}"
