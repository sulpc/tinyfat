# tinyfat

> "just for fun!"

A toy file system.

FAT32 is ugly, choose it only for convenience to debug or use.

Unfinished, only implement the file read functions simply.

Files need to be modified for migration:

- `tinyfat_disk.c`: base functions to read and write disks, should be implemented
- `tinyfat_cfg.h`: some configs
- `main.c`: main test file, use a vhd (MBR+FAT32)