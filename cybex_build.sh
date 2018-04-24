#!/bin/bash
###########################################################
# This is the CybexIO automated install script for Mac OS.
# This file was downloaded from https://github.com/NebulaCybexDEX/bitshares-core.git
#
# Copyright (c) 2018, Respective Authors all rights reserved.
#
###########################################################


VERSION=1.2
ULIMIT=$( ulimit -u )
WORK_DIR=$PWD
BUILD_DIR=${WORK_DIR}/build
ARCH=$( uname -s )

#cd ${WORK_DIR}
if [ ! -d ${BUILD_DIR} ];
then
    mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}


#cmake -DCMAKE_BUILD_TYPE=Release   ..
cmake -G"Unix Makefiles"  -DOPENSSL_ROOT_DIR=/usr/local/Cellar/openssl/1.0.2o_1  -DBOOST_ROOT=/usr/local/Cellar/boost@1.57/1.57.0/ .. -B.



if [ "${ARCH}" == "Darwin" ];
    then
        N_CPU=$(sysctl -i machdep.cpu.thread_count | awk '{print $2}')
    else
        N_CPU=$(cat /proc/cpuinfo | grep -c  '^processor')

fi

make -j${N_CPU} all


