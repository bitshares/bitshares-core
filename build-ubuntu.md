
The Boost which ships with Ubuntu 14.04 LTS is too old.  You need to download the Boost tarball for Boost 1.57.0
(Note, 1.58.0 requires C++14 and will not build on Ubuntu LTS; this requirement was an accident, see ).  Build Boost as follows:

    # tarball available at http://sourceforge.net/projects/boost/files/boost/1.57.0/boost_1_57_0.tar.bz2/download
    # sha256sum is 910c8c022a33ccec7f088bd65d4f14b466588dda94ba2124e78b8c57db264967

    BOOST_ROOT=$(echo ~/opt/boost_1_57_0)

    # build Boost from source
    cd ~/src/boost_1_57_0
    ./bootstrap.sh --prefix=$BOOST_ROOT
    ./b2 link=static variant=debug threading=multi stage
    ./b2 link=static variant=debug threading=multi install

Then we need to tell `cmake` to use the Boost we just built, instead of using the system-wide Boost:

    cd ~/src/graphene
    [ -e ./doc/build-ubuntu.md ] && sh -c 'cmake -DBOOST_ROOT="$BOOST_ROOT" -DCMAKE_BUILD_TYPE=Debug .'

If all goes well, you should see the correct Boost version in the output messages to the above command.
