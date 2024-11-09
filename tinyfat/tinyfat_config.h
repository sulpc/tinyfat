#pragma once

// config
#define TF_MAX_FS_NUM          1     //
#define TF_DEFALUT_SECTOR_SIZE 512   //
#define TF_FN_LEN_MAX          13    // format: XXXXXXXX.XXX + '\0'
#define TF_SFN_LEN             12    // 8 + 3 + '\0'
#define TF_LFN_SUPPORTTED      0     // long filename supported
#ifdef HOST_DEBUG
#define TF_WITH_MBR            1     // set `1` for vhd file
#else
#define TF_WITH_MBR            0     // sdcard without MBR
#endif

#define tf_logger(...)         // util_printf(__VA_ARGS__)
