#pragma once

#include "tinyfat_config.h"
#include "tinyfat_path.h"
#include "util_types.h"

#define TF_ERR_PARAM             -1
#define TF_ERR_PATH              -5
#define TF_ERR_MOUNT_LABEL_USED  -7
#define TF_ERR_NO_FAT32LBA       -2
#define TF_ERR_DEV_NOTMOUNT      -4
#define TF_ERR_FNAME_IVALID      -10
#define TF_ERR_LFN_NOT_SUPPORTED -9
#define TF_ERR_SECTORSIZE        -12
#define TF_ERR_DISKACCESS        -13

// item attr
#define TF_ATTR_READ_ONLY 0x01
#define TF_ATTR_HIDDEN    0x02
#define TF_ATTR_SYSTEM    0x04
#define TF_ATTR_VOLUME_ID 0x08
#define TF_ATTR_DIRECTORY 0x10
#define TF_ATTR_ARCHIVE   0x20

#define tf_dir_open   tf_item_open
#define tf_dir_close  tf_item_close
#define tf_file_open  tf_item_open
#define tf_file_close tf_item_close

typedef struct tf_fs_t tf_fs_t;

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minite;
    uint8_t  second;
} tf_time_t;

typedef struct {
    uint8_t   attr;              // bitmap of TF_ATTR_*
    char      sfn[TF_SFN_LEN];   //
    uint32_t  size;              // size of file
    uint32_t  first_clus;        // first cluster id (start at 2)
    uint32_t  cur_clus;          //
    uint32_t  cur_ofs;           // current byte offset
    tf_time_t write_time;
    tf_time_t create_time;
    tf_fs_t*  fs;
} tf_item_t;


#define tf_dir_t  tf_item_t
#define tf_file_t tf_item_t

/**
 * @brief mount a device to file system
 *
 * @param device device id
 * @param label should be a printable char
 * @return int 0-ok, other-fail
 */
int tf_mount(int device, char label);

/**
 * @brief unmount device
 *
 * @param device device id
 * @return int 0-ok, other-fail
 */
int tf_unmount(int device);

/**
 * @brief open a file or dir
 *
 * @param path absolute path, like "/xxx" or "x:/xxx"
 * @param item the file or dir at the path, result value
 * @return int 0-ok, other-fail
 */
int tf_item_open(const char* path, tf_item_t* item);

/**
 * @brief close a file or dir
 *
 * @param dir
 * @return int 0-ok, other-fail
 */
int tf_item_close(tf_item_t* item);

/**
 * @brief read item from dir
 *
 * @param dir should be dir really
 * @param item the item read from the dir, result value
 * @return int 0-ok, positive-has end, negtive-fail
 */
int tf_dir_read(tf_dir_t* dir, tf_item_t* item);

/**
 * @brief read file content, once read, the file ptr will move
 *
 * @param file should be really file
 * @param buffer should be large enough to store the data you want
 * @param size the data size wanted
 * @return int the data size really read
 */
int tf_file_read(tf_file_t* file, uint8_t* buffer, uint32_t size);

/**
 * @brief read a sector from disk, CALLOUT
 *
 * @param device device id
 * @param sec_id sector id
 * @param sec_size sector size
 * @param data data buffer
 * @return int 0-ok, other-fail
 */
extern int tf_disk_read(int device, uint32_t sec_id, uint16_t sec_size, uint8_t* data);

// tbd
/*
int tf_format();
int tf_dir_create();
int tf_file_seek();
int tf_file_write();
*/
