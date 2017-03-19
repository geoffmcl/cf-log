#!/bin/sh
#< build-me.sh - cf-log - 20140504
BN=`basename $0`
TMPPRJ="cf-log"
TMPLOG="bldlog-1.txt"
TMPSG="/home/geoff/fg/next/install/simgear"

TMPOPTS=""
# Possible options to help with problems
# TMPOPTS="$TMPOPTS -DCMAKE_VERBOSE_MAKEFILE=ON"

for arg in $@; do
	TMPOPTS="$TMPOPTS $arg"
done

if [ -z "$TMPOPTS" ]; then
	TMPOPTS="-DCMAKE_INSTALL_PREFIX=$HOME/projects/install/$TMPPRJ"
fi
if [ -d "$TMPSG" ]; then
    TMPOPTS="$TMPOPTS -DUSE_SIMGEAR_LIB:BOOL=TRUE"
    TMPOPTS="$TMPOPTS -DCMAKE_PREFIX_PATH=$TMPSG"
fi
if [ -f "$TMPLOG" ]; then
    rm -f $TMPLOG
fi

echo "Begin $TMPPRJ project build" > $TMPLOG

echo "Doing 'cmake .. $TMPOPTS'"
echo "Doing 'cmake .. $TMPOPTS'" >> $TMPLOG
cmake .. $TMPOPTS >> $TMPLOG 2>&1
if [ ! "$?" =  "0" ]; then
	echo "cmake config, gen error $?"
	echo "See $TMPLOG for details..."
	exit 1
fi

echo "Doing 'make'"
echo "Doing 'make'" >>$TMPLOG
make >>$TMPLOG 2>&1
if [ ! "$?" =  "0" ]; then
	echo "make error $?"
	echo "See $TMPLOG for details..."
	exit 1
fi

echo "Appears a successful build..."

echo "Perhaps follow with 'make install'"


# eof

