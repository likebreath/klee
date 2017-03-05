#!/bin/bash -x

set -e

export C_INCLUDE_PATH=/usr/include/x86_64-linux-gnu
export CPLUS_INCLUDE_PATH=/usr/include/x86_64-linux-gnu

## A workaround for lacking x86_64-linux-gnu-ar/x86_64-linux-gnu-ranlib on
## Ubuntu 12.04
TEMP_BIN="/tmp/temp_bin/"
mkdir -p $TEMP_BIN
if [ ! -x x86_64-linux-gnu-ar ]; then
    AR=`which ar`
    ln -sf $AR $TEMP_BIN/x86_64-linux-gnu-ar
fi
if [ ! -x x86_64-linux-gnu-ranlib ]; then
    RANLIB=`which ranlib`
    ln -sf $RANLIB $TEMP_BIN/x86_64-linux-gnu-ranlib
fi
export PATH=$PATH:$TEMP_BIN
