
* tessa.conf: contains configuration settings for tessad or tessa-qt
* tessad.pid: stores the process id of tessad while running
* blocks/blk000??.dat: block data (custom, 128 MiB per file); since 0.8.0
* blocks/rev000??.dat; block undo data (custom); since 0.8.0 (format changed since pre-0.8)
* blocks/index/*; block index (LevelDB); since 0.8.0
* chainstate/*; block chain state database (LevelDB); since 0.8.0
* debug.log: contains debug information and general logging generated by tessad or tessa-qt
* peers.dat: peer IP address database (custom format); since 0.7.0
* data.mbd: personal wallet with keys and transactions

