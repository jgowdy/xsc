#!/bin/bash
# GCC wrapper to force temp directory usage
export TMPDIR=/storage/icloud-backup/build/xsc-bootstrap/tmp
export TEMP=$TMPDIR
export TMP=$TMPDIR
exec /usr/bin/gcc "$@"
