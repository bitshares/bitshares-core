#!/bin/bash
###########################################################
# This is the CybexIO automated install script for Mac OS.
# This file was downloaded from https://github.com/NebulaCybexDEX/bitshares-core.git
#
# Copyright (c) 2018, Respective Authors all rights reserved.
#
###########################################################


if [ $# -gt 0 ] && [ "$1" == "prod" ];
then
   source build_number
   build=$(( ${BUILD} +1 ))

   install_tag=-$(date "+%Y%m%d%H%M%S")."${build}"
   echo "BUILD=${build}">build_number
else
   install_tag=
fi


VERSION=1.2
ULIMIT=$( ulimit -u )
WORK_DIR=$PWD
BUILD_DIR=${WORK_DIR}/build
ARCH=$( uname -s )


if [ ! -d ${BUILD_DIR} ];
then
    mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}



if [ "${ARCH}" == "Darwin" ];
    then
        cmake -G"Unix Makefiles"  -DCMAKE_INSTALL_PREFIX="$PWD/install${install_tag}" -DGRAPHENE_EGENESIS_JSON=genesis.json -DOPENSSL_ROOT_DIR=/usr/local/Cellar/openssl/1.0.2o_1  -DBOOST_ROOT=/usr/local/Cellar/boost@1.57/1.57.0/ .. -B.
        N_CPU=$(sysctl -i machdep.cpu.thread_count | awk '{print $2}')
    else
        cmake -G"Unix Makefiles"  -DCMAKE_INSTALL_PREFIX="$PWD/install${install_tag}" -DGRAPHENE_EGENESIS_JSON=genesis.json .. -B.
        N_CPU=$(cat /proc/cpuinfo | grep -c  '^processor')

fi

make -j${N_CPU} all install

cd ${WORK_DIR}
