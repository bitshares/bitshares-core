== Graphene Client GUI ==

This is a Qt-based native GUI client for Graphene blockchains.

To build this GUI, run cmake with -DBUILD_QT_GUI=ON

This GUI depends on Qt 5.5 or later. If you do not have Qt 5.5 installed
in the canonical location on your OS (or if your OS does not have a
canonical location for libraries), you can specify the Qt path by running
cmake with -DCMAKE_PREFIX_PATH=/path/to/Qt/5.5/gcc_64 as appropriate for
your environment.
