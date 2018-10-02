UNIX BUILD NOTES
====================
Some notes on how to build Tessa in Unix.

To Build
---------------------

```bash
mkdir build
cd build
cmake ..
make
```

This will build tessa-qt as well if the dependencies are met.

Dependencies
---------------------

These dependencies are required:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 libevent    | Events           | Asynchronous event notification
 libsodium   | Cryptography     | Cryptographic library
 libgmp      | Math library     | Math library
 rocksdb     | Database         | Facebook database key/value storage library

Optional dependencies:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 miniupnpc   | UPnP Support     | Firewall-jumping support
 qt          | GUI              | GUI toolkit (only needed when GUI enabled)
 libqrencode | QR codes in GUI  | Optional for generating QR codes (only needed when GUI enabled)
 zeromq      |   |

For the versions used in the release, see [release-process.md](release-process.md) under *Fetch and build inputs*.

System requirements
--------------------

C++ compilers are memory-hungry. It is recommended to have at least 1 GB of
memory available when compiling Core. With 512MB of memory or less
compilation will take much longer due to swap thrashing.

Dependency Build Instructions: Ubuntu & Debian
----------------------------------------------
Build requirements: (check packages names ***)

	sudo apt-get install build-essential cmake libevent-dev rocksdb

Optional:

	sudo apt-get install libminiupnpc-dev zeromq 

Dependencies for the GUI: Ubuntu & Debian
-----------------------------------------

If you want to build Tessa-Qt, make sure that the required packages for Qt development
are installed. Qt 5 is necessary to build the GUI.

For Qt 5 you need the following:

    sudo apt-get install libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools 

libqrencode (optional) can be installed with:

    sudo apt-get install libqrencode-dev

Once these are installed, they will be found by cmake and tessa-qt executable will be built by default.

