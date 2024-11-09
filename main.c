#include "tinyfat.h"
#include "tinyfat_path.h"
#include <stdio.h>
#include <stdlib.h>

#define MY_DISK_ID 0

char* vhdfilepath;

int tf_disk_read(int device, uint32_t sec_id, uint16_t sec_size, uint8_t* data)
{
    if (device != MY_DISK_ID) {
        return -1;
    }

    FILE* vhd = fopen(vhdfilepath, "rb");   // vhd: MBR+FAT32
    fseek(vhd, sec_id * sec_size, SEEK_SET);
    fread(data, 1, sec_size, vhd);
    fclose(vhd);

    return 0;
}

int main(int argc, char* argv[])
{
    int         ret;
    tf_item_t   dir, item;
    const char* path;
    uint8_t     buffer[4096] = {0};
    char        name[16]     = {0};

    if (argc < 3) {
        printf("usage: cmd <vhdfile> <path>\n");
        return 0;
    }

    vhdfilepath = argv[1];
    path        = argv[2];

    ret = tf_mount(MY_DISK_ID, 'X');
    if (ret != 0) {
        printf("ERROR %d\n", ret);
        exit(0);
    }

    ret = tf_item_open(path, &dir);
    if (ret != 0) {
        printf("ERROR %d\n", ret);
        exit(0);
    }

    if (dir.attr & TF_ATTR_ARCHIVE) {
        printf("file<%s>:\n", path);
        int read;

        while ((read = tf_file_read(&dir, buffer, 10)) > 0) {
            buffer[read] = '\0';
            printf("%s", buffer);
        }
        printf("\n");
    } else {
        printf("dir<%s>:\n", path);

        while (tf_dir_read(&dir, &item) == 0) {
            if ((item.attr & TF_ATTR_HIDDEN) || (item.attr & TF_ATTR_SYSTEM)) {
                continue;
            }

            if (item.attr & TF_ATTR_DIRECTORY) {
                printf("\033[34m");
            } else {   // (item.attr & TF_ATTR_ARCHIVE)
                printf("\033[32m");
            }

            tf_sfn2name(item.sfn, name);
            // printf("`%s` %s\n", item.sfn, name);
            printf("%s\033[0m ", name);
        }
        printf("\n");
    }

    tf_unmount(MY_DISK_ID);

    printf("bye.\n");
}
