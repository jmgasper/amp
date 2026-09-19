#!/bin/sh
set -eu
# Refresh the per-user MIME record after a package upgrade.
TA_EXECUTABLE=$(findpaths -p "$0" B_FIND_PATH_APPS_DIRECTORY TasAmp)
mimeset -a -f "$TA_EXECUTABLE"
