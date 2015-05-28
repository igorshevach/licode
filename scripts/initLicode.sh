#!/bin/bash

SCRIPT=`pwd`/$0
FILENAME=`basename $SCRIPT`
PATHNAME=`dirname $SCRIPT`
ROOT=$PATHNAME/..
BUILD_DIR=$ROOT/build
CURRENT_DIR=`pwd`
EXTRAS=$ROOT/extras

killall -9 node nodejs

rm -f  $ROOT/erizo_controller/erizoAgent/*.log 

export PATH=$PATH:/usr/local/sbin

if ! pgrep -f rabbitmq; then
  sudo echo
  sudo rabbitmq-server > $BUILD_DIR/rabbit.log &
fi

cd $ROOT/nuve
./initNuve.sh

sleep 1

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$ROOT/erizo/build/erizo:$ROOT/erizo:$ROOT/build/libdeps/build/lib
export ERIZO_HOME=$ROOT/erizo/

cd  $ROOT/erizo_controller/erizoClient/tools

./compile.sh
./compilefc.sh

cd $ROOT/erizo_controller
./initErizo_controller.sh
./initErizo_agent.sh


cp $ROOT/erizo_controller/erizoClient/dist/erizo.js $EXTRAS/basic_example/public/
cp $ROOT/nuve/nuveClient/dist/nuve.js $EXTRAS/basic_example/

#echo [licode] Done, run basic_example/basicServer.js

cd $ROOT/scripts
./initBasicExample.sh

