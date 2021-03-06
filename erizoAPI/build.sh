#!/bin/bash

SCRIPT_DIR=`readlink -f $(dirname $0) `

pushd `pwd`

cd $SCRIPT_DIR

if [[ $# ]] ; then
  BUILDTYPE=$1
  [ $1 = 'Debug' ] && CONF=--debug
fi 

echo "
ERIZO_HOME=$ERIZO_HOME
LD_LIBRARY_PATH=$LD_LIBRARY_PATH
CONF=$CONF
\$@=$@
BUILDTYPE=$BUILDTYPE
"

if hash node-waf 2>/dev/null; then
  echo 'building with node-waf'
  rm -rf build
  node-waf configure build $CONF
else
  echo 'building with node-gyp'
  node-gyp rebuild $CONF
fi

if [ -n $BUILDTYPE ]; then
	LINK_DIR=`readlink -e $ERIZO_HOME/../erizoAPI/build`
	echo "check if $LINK_DIR/$BUILDTYPE exists..."
	if [ -d $LINK_DIR/$BUILDTYPE ]; then
		[ -e $LINK_DIR/addon.node ] && rm -f $LINK_DIR/addon.node
		ln -sf $LINK_DIR/$BUILDTYPE/addon.node $LINK_DIR  
		echo "creating symlink to $LINK_DIR/$BUILDTYPE"
	fi
fi
popd
