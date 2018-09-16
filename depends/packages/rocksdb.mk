package=rocksdb
$(package)_version=5.15.10
$(package)_download_path=https://github.com/facebook/rocksdb/archive/
$(package)_file_name=v5.15.10.tar.gz
$(package)_sha256_hash=26d5d4259fa352ae1604b5b4d275f947cacc006f4f7d2ef0b815056601b807c0

define $(package)_build_cmds
  $(MAKE) static_lib 
endef

define $(package)_stage_cmds
  $(MAKE) install-static INSTALL_PATH=/users/spiralus/tessa/x86_64-w64-mingw32
endef
