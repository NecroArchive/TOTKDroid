# zstd (decompression only)

Zstandard 1.5.7 (https://github.com/facebook/zstd, BSD licence: LICENSE), as its single-file decoder:
`zstddeclib.c` made with `build/single_file_libs/combine.py -r ../../lib -x legacy/zstd_legacy.h -o zstddeclib.c
zstddeclib-in.c`, and `zstd.h` / `zstd_errors.h` from `lib/`. The runtime reads a few of the game's own `.zs` files
with it (services/fs.c fs_romfs_read_zs: the touch controls' item icons).
