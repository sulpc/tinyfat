#include "tinyfat.h"
#include "tinyfat_mem.h"
#include "tinyfat_path.h"
#include "util_queue.h"

#define TF_DIRITEM_SIZE           32
#define TF_SECTOR_SIZE_MAX        512
#define TF_CACHE_NUM              (TF_SECTOR_SIZE_MAX / 4)
#define TF_CLUSTER_ID_VALID(clus) (clus < 0x0FFFFFF8)
#define TF_INVALID_SECTOR_ID      0xffffffff
#define TF_INVALID_CLUSTER_ID     0xffffffff
#define TF_ATTR_LFN               0x0F   // lfn item
#define TF_ATTR_DELETED           0xE5   // deleted item
#define TF_ATTR_EMPTY             0x00   // empty
#define TF_MASK_MATCH(attr, mask) (((attr) & (mask)) == (mask))


struct tf_fs_t {
    uint8_t           device;                      // device id
    char              label;                       // label, '\0' mean not used
    uint16_t          sec_size;                    // BS: sector size
    uint8_t           clus_sec_num;                // BS: sector count of a cluster
    uint32_t          sec_num_total;               // BS: sector count of volume
    uint32_t          free_clus_num;               // FSInfo: FSI_Free_Count
    uint32_t          next_free_clus;              // FSInfo: FSI_Nxt_Free
    uint32_t          fat_sec_ofs;                 // sector offset of FAT area in all DISK
    uint32_t          dat_sec_ofs;                 // sector offset of DATA area in DISK
    uint8_t           cache[TF_SECTOR_SIZE_MAX];   //
    uint32_t          cache_sec_id;                // cache sector id
    uint32_t          fatcache[TF_CACHE_NUM];      // FAT table cache, next cluster id
    uint32_t          fatcache_start;              // fatcache start cluster id
    util_queue_node_t qnode;
};


static util_queue_node_t fs_list = {
    .next = &fs_list,
    .prev = &fs_list,
};


/**
 * @brief get next cluster id from fat table
 *
 * @param fs
 * @param clus_id current cluster id
 * @return uint32_t
 */
static uint32_t tf_next_cluster(tf_fs_t* fs, uint32_t clus_id)
{
    if (clus_id < fs->fatcache_start || clus_id - fs->fatcache_start >= TF_CACHE_NUM) {
        // not hit
        fs->fatcache_start = clus_id & (~(uint32_t)(TF_CACHE_NUM - 1));
        tf_disk_read(fs->device, fs->fat_sec_ofs + fs->fatcache_start / TF_CACHE_NUM, fs->sec_size,
                     (uint8_t*)fs->fatcache);
    }
    return fs->fatcache[clus_id - fs->fatcache_start];
}


/**
 * @brief read one sector to cache
 *
 * @param fs
 * @param sec_id
 * @return int
 */
static int tf_fs_disk_read(tf_fs_t* fs, uint32_t sec_id)
{
    int ret = 0;
    if (sec_id != fs->cache_sec_id) {
        ret = tf_disk_read(fs->device, sec_id, fs->sec_size, fs->cache);
        if (ret == 0) {
            fs->cache_sec_id = sec_id;
        }
    }
    return ret;
}


/**
 * @brief fetch file data from disk to cache
 *
 * @param item: file or dir
 * @return 0-fetch ok, positive-no data, negtive-fetch fail
 */
static int tf_item_data_fetch(tf_item_t* item)
{
    tf_fs_t* fs           = item->fs;
    uint16_t cur_clus_ofs = item->cur_ofs % (fs->sec_size * fs->clus_sec_num);   // offset in current cluster

    // if current cluster read finished, try find next cluster
    if (item->cur_ofs != 0 && cur_clus_ofs == 0) {
        // find next cluster
        uint32_t next_clus = tf_next_cluster(fs, item->cur_clus);

        if (TF_CLUSTER_ID_VALID(next_clus)) {
            item->cur_clus = next_clus;
        } else {
            // no next cluster
            return 1;
        }
    }

    uint32_t sec_id = fs->dat_sec_ofs + fs->clus_sec_num * (item->cur_clus - 2) + (cur_clus_ofs / fs->sec_size);
    return tf_fs_disk_read(fs, sec_id);
}


/**
 * @brief parse a directory item from raw data
 *
 * @param raw dir item, 32 bytes
 * @param item
 */
static void tf_item_parse(uint8_t* raw, tf_item_t* item)
{
    uint8_t attr = (uint8_t)util_bytes2uint_le(raw + 11, 1);   // DIR_Attr  11  1

    if (attr == 0 || raw[0] == 0) {
        item->attr = TF_ATTR_EMPTY;
    } else if (raw[0] == 0xE5) {
        item->attr = TF_ATTR_DELETED;
    } else if (TF_MASK_MATCH(attr, TF_ATTR_LFN)) {
        // lfn, will be ignored
        item->attr = attr;
    } else {
        // sfn
        item->attr = attr;

        memcpy(item->sfn, raw + 0, 11);   // DIR_Name
        item->sfn[TF_SFN_LEN - 1] = '\0';

        uint16_t temp           = (uint16_t)util_bytes2uint_le(raw + 24, 2);   // DIR_WrtDate
        item->write_time.year   = 1980 + (temp >> 9);
        item->write_time.month  = (temp >> 9) & 0x0F;
        item->write_time.day    = temp & 0x1F;
        temp                    = (uint16_t)util_bytes2uint_le(raw + 22, 2);   // DIR_WrtTime
        item->write_time.year   = temp >> 11;
        item->write_time.month  = (temp >> 5) & 0x3F;
        item->write_time.day    = temp & 0x1F;
        temp                    = (uint16_t)util_bytes2uint_le(raw + 16, 2);   // DIR_CrtDate
        item->create_time.year  = 1980 + (temp >> 9);
        item->create_time.month = (temp >> 9) & 0x0F;
        item->create_time.day   = temp & 0x1F;
        temp                    = (uint16_t)util_bytes2uint_le(raw + 14, 2);   // DIR_CrtTime
        item->create_time.year  = temp >> 11;
        item->create_time.month = (temp >> 5) & 0x3F;
        item->create_time.day   = temp & 0x1F;

        item->first_clus = (util_bytes2uint_le(raw + 20, 2) << 16) | util_bytes2uint_le(raw + 26, 2);
        item->size       = util_bytes2uint_le(raw + 28, 4);
        item->cur_clus   = item->first_clus;
        item->cur_ofs    = 0;
    }
}


/**
 * @brief
 *
 * @param path like 'xxx/xxx/xxx'
 * @param name
 * @return int
 */
static int tf_get_base_of_path(const char* path, char* name)
{
    int i = 0;
    while (path[i] != '\0' && path[i] != '/' && i < TF_FN_LEN_MAX) {
        name[i] = path[i];
        i++;
    }
    if (path[i] != '\0' && path[i] != '/') {
        return TF_ERR_LFN_NOT_SUPPORTED;
    }
    name[i] = '\0';
    return i;
}


/**
 * @brief find item of subpath from a dir
 *
 * @param dir
 * @param subpath not start by '/'
 * @param item return the item found in the dir
 * @return int 0-ok
 */
static int tf_item_find(const tf_item_t* dir, const char* subpath, tf_item_t* item)
{
    if (dir == nullptr || subpath == nullptr || item == nullptr) {
        return TF_ERR_PARAM;
    }
    if (!TF_MASK_MATCH(dir->attr, TF_ATTR_DIRECTORY)) {
        return TF_ERR_PARAM;
    }

    static tf_item_t base;
    static char      name[TF_FN_LEN_MAX] = {0};
    static char      sfn[TF_SFN_LEN]     = {0};

    memcpy(&base, dir, sizeof(tf_item_t));

    while (true) {
        tf_logger("[%s] enter dir `%s`, try find `%s`\n", __func__, dir->sfn, subpath);

        if (subpath[0] == '\0') {
            // subpath like "a/b/c/"
            tf_logger("[%s] found `%s`\n", __func__, subpath);
            memcpy(item, &base, sizeof(tf_item_t));
            return 0;
        }

        if (subpath[0] == '/') {
            return TF_ERR_PATH;
        }

        // first part of subpath
        int sep = tf_get_base_of_path(subpath, name);
        tf_name2sfn(name, sfn);

        bool part_found = false;
        while (tf_dir_read(&base, item) == 0) {
            if (strcmp(item->sfn, sfn) == 0) {
                if (strcmp(name, "..") == 0 && item->first_clus == 0) {   // upper is the root dir
                    item->cur_clus = item->first_clus = 2;
                }

                if (subpath[sep] == '\0') {   // item is the wanted file/dir
                    tf_logger("[%s] found `%s`\n", __func__, subpath);
                    return 0;
                }

                // confirm item is a dir
                if (!TF_MASK_MATCH(item->attr, TF_ATTR_DIRECTORY)) {
                    return TF_ERR_PATH;
                }

                memcpy(&base, item, sizeof(tf_item_t));
                subpath += sep + 1;
                part_found = true;
                break;
            }
        }
        if (!part_found) {
            return TF_ERR_PATH;
        }
    }
}


int tf_mount(int device, char label)
{
    tf_fs_t* fs         = nullptr;
    uint16_t volume_ofs = 0;   // fat32 volume sector offset

    util_queue_foreach(node, &fs_list)
    {
        tf_fs_t* tmp = util_containerof(tf_fs_t, qnode, node);
        if (tmp->label == label) {
            return TF_ERR_MOUNT_LABEL_USED;
        }
    }

    fs = (tf_fs_t*)tf_malloc(sizeof(tf_fs_t));
    memset(fs, 0, sizeof(tf_fs_t));
    fs->label          = label;
    fs->device         = device;
    fs->sec_size       = TF_DEFALUT_SECTOR_SIZE;
    fs->cache_sec_id   = TF_INVALID_SECTOR_ID;
    fs->fatcache_start = TF_INVALID_CLUSTER_ID;

    tf_logger("[%s] fs label='%c', device=%d\n", __func__, fs->label, fs->device);

#if TF_WITH_MBR
    // read first sector, find first FAT32(LBA) partition
    tf_fs_disk_read(fs, 0);

    int i = 0;
    for (i = 0; i < 4; i++) {
        if (util_bytes2uint_le(fs->cache + 446 + 16 * i + 4, 1) == 0x0C) {   // 0x0C: FAT32 (LBA)
            volume_ofs = util_bytes2uint_le(fs->cache + 446 + 16 * i + 8, 4);
            break;
        }
    }
    if (i == 4) {   // no fat32lba partition
        tf_free(fs);
        return TF_ERR_NO_FAT32LBA;
    }
#endif

    tf_logger("[%s] fs sector offset=%d\n", __func__, volume_ofs);

    util_queue_insert(&fs_list, &fs->qnode);

    // read Boot sector, make sure the volume is FAT32LBA
    if (tf_fs_disk_read(fs, volume_ofs) != 0) {
        tf_free(fs);
        return TF_ERR_DISKACCESS;
    }

    fs->sec_size            = util_bytes2uint_le(fs->cache + 11, 2);   // BPB_BytsPerSec
    fs->clus_sec_num        = util_bytes2uint_le(fs->cache + 13, 1);   // BPB_SecPerClus
    uint16_t resv_sec_num   = util_bytes2uint_le(fs->cache + 14, 2);   // BPB_RsvdSecCnt
    uint8_t  fat_num        = util_bytes2uint_le(fs->cache + 16, 1);   // BPB_NumFATs
    uint32_t hidden_sec_num = util_bytes2uint_le(fs->cache + 28, 4);   // BPB_HiddSec
    fs->sec_num_total       = util_bytes2uint_le(fs->cache + 32, 4);   // BPB_TotSec32
    uint32_t fat_sec_num    = util_bytes2uint_le(fs->cache + 36, 4);   // BPB_FATSz32
    uint16_t fsinfo_sec     = util_bytes2uint_le(fs->cache + 48, 2);   // BPB_FSInfo

    util_unused(hidden_sec_num);

    tf_logger("[%s] fs sec_size=%d\n", __func__, fs->sec_size);
    tf_logger("[%s] fs clus_sec_num=%d\n", __func__, fs->clus_sec_num);
    tf_logger("[%s] fs resv_sec_num=%d\n", __func__, resv_sec_num);
    tf_logger("[%s] fs fat_num=%d\n", __func__, fat_num);
    tf_logger("[%s] fs hidden_sec_num=%d\n", __func__, hidden_sec_num);
    tf_logger("[%s] fs sec_num_total=%d\n", __func__, fs->sec_num_total);
    tf_logger("[%s] fs fat_sec_num=%d\n", __func__, fat_sec_num);
    tf_logger("[%s] fs fsinfo_sec=%d\n", __func__, fsinfo_sec);

    if (fs->sec_size > TF_SECTOR_SIZE_MAX) {
        tf_free(fs);
        return TF_ERR_SECTORSIZE;
    }

    // read FSInfo sector
    if (tf_fs_disk_read(fs, volume_ofs + fsinfo_sec) != 0) {
        tf_free(fs);
        return TF_ERR_DISKACCESS;
    }

    fs->free_clus_num  = util_bytes2uint_le(fs->cache + 488, 4);   // FSI_Free_Count
    fs->next_free_clus = util_bytes2uint_le(fs->cache + 492, 4);   // FSI_Nxt_Free
    tf_logger("[%s] fs free_clus_num=%d\n", __func__, fs->free_clus_num);
    tf_logger("[%s] fs next_free_clus=%d\n", __func__, fs->next_free_clus);

    fs->fat_sec_ofs = volume_ofs + resv_sec_num;
    fs->dat_sec_ofs = fs->fat_sec_ofs + fat_sec_num * fat_num;
    tf_logger("[%s] fs fat_sec_ofs=%d\n", __func__, fs->fat_sec_ofs);
    tf_logger("[%s] fs dat_sec_ofs=%d\n", __func__, fs->dat_sec_ofs);

    return 0;
}


int tf_unmount(int device)
{
    tf_fs_t* fs = nullptr;
    util_queue_foreach(node, &fs_list)
    {
        tf_fs_t* tmp = util_containerof(tf_fs_t, qnode, node);
        if (tmp->device == device) {
            fs = tmp;
            break;
        }
    }

    if (fs == nullptr) {
        return TF_ERR_DEV_NOTMOUNT;
    }

    util_queue_remove(&fs->qnode);
    tf_free(fs);
    return 0;
}


int tf_item_open(const char* path, tf_item_t* item)
{
    if (path == nullptr || item == nullptr) {
        return TF_ERR_PARAM;
    }

    int pathlen = strlen(path);
    if (pathlen == 0) {
        return TF_ERR_PATH;
    }
    if (!(path[0] == '/') && !(pathlen >= 3 && path[1] == ':' && path[2] == '/')) {
        return TF_ERR_PATH;
    }

    const char* subpath = nullptr;
    tf_fs_t*    fs      = nullptr;

    if (!util_queue_empty(&fs_list)) {
        if (path[0] == '/') {
            subpath = &path[1];
            fs      = util_containerof(tf_fs_t, qnode, fs_list.next);
        } else {
            util_queue_foreach(node, &fs_list)
            {
                tf_fs_t* tmp = util_containerof(tf_fs_t, qnode, node);
                if (tmp->label == path[0]) {
                    fs      = tmp;
                    subpath = &path[3];
                    break;
                }
            }
        }
    }
    if (fs == nullptr) {
        return TF_ERR_PATH;
    }

    // set item as root dir, cluster id start at 2
    item->fs         = fs;
    item->attr       = TF_ATTR_DIRECTORY;
    item->sfn[0]     = fs->label;
    item->sfn[1]     = '\0';
    item->first_clus = 2;   // cluster no. start from 2
    item->cur_clus   = item->first_clus;
    item->cur_ofs    = 0;

    // search subpath
    return tf_item_find(item, subpath, item);
}


int tf_item_close(tf_item_t* item)
{
    if (item == nullptr) {
        return TF_ERR_PARAM;
    }
    memset(item, 0, sizeof(tf_item_t));
    return 0;
}


int tf_dir_read(tf_dir_t* dir, tf_item_t* item)
{
    if (dir == nullptr || item == nullptr) {
        return TF_ERR_PARAM;
    }
    if (!TF_MASK_MATCH(dir->attr, TF_ATTR_DIRECTORY)) {
        return TF_ERR_PARAM;
    }

    tf_fs_t* fs          = dir->fs;
    uint32_t dir_ofs_bak = dir->cur_ofs;

    while (true) {
        int prefetch = tf_item_data_fetch(dir);
        if (prefetch < 0) {
            dir->cur_ofs = dir_ofs_bak;
            return TF_ERR_DISKACCESS;
        }
        if (prefetch > 0) {
            return 1;
        }

        tf_item_parse(fs->cache + (dir->cur_ofs % fs->sec_size), item);
        dir->cur_ofs += TF_DIRITEM_SIZE;

        item->fs = dir->fs;

        if (item->attr == TF_ATTR_EMPTY) {   // empty item, end
            return 1;
        }
        if (item->attr == TF_ATTR_DELETED) {   // deleted item, ignore it
            continue;
        }
        if (TF_MASK_MATCH(item->attr, TF_ATTR_LFN)) {   // lfn item, ignore it
            continue;
        } else {   // sfn
            return 0;
        }
    }
}


int tf_file_read(tf_file_t* file, uint8_t* buffer, uint32_t size)
{
    if (file == nullptr || buffer == nullptr) {
        return TF_ERR_PARAM;
    }
    if (!TF_MASK_MATCH(file->attr, TF_ATTR_ARCHIVE)) {
        return TF_ERR_PARAM;
    }

    uint32_t size_read    = 0;
    tf_fs_t* fs           = file->fs;
    uint32_t file_ofs_bak = file->cur_ofs;

    size = util_min2(size, file->size - file->cur_ofs);

    while (size_read < size) {
        int ret = tf_item_data_fetch(file);
        if (ret < 0) {
            file->cur_ofs = file_ofs_bak;
            return TF_ERR_DISKACCESS;
        }

        if (ret > 0) {   // no data to fetch
            break;
        }

        // read the data in current sector
        uint16_t ofs     = file->cur_ofs % fs->sec_size;
        uint16_t readnow = util_min2(size, fs->sec_size - ofs);

        memcpy(&buffer[size_read], &fs->cache[ofs], readnow);
        file->cur_ofs += readnow;
        size_read += readnow;
    }

    return size_read;
}
