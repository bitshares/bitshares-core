This directory contains the files needed to make the stock LevelDB distribution
from https://github.com/bitcoin/leveldb.git compile on Windows with Visual C++.
Add this 'include' directory to yur include path before the regular includes
only when building leveldb (it isn't needed when compiling code that uses 
leveldb).