#include "tinyfat.h"
#include "util_misc.h"

// if name not accord with 8dot3, the sfn will be wrong
int tf_name2sfn(const char* name, char* sfn)
{
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        memcpy(sfn, name, strlen(name));
        return 0;
    }

    memset(sfn, ' ', 11);
    sfn[11] = '\0';

    uint8_t i;
    for (i = 0; *name != '\0' && *name != '.' && i < 8; i++) {
        sfn[i] = toupper(*name++);
    }

    if (*name != '\0') {
        if (*name != '.') {
            return TF_ERR_FNAME_IVALID;
        }

        name++;
        if (*name == '\0') {   // name may like: "xxx."
            sfn[i]     = '.';
            sfn[i + 1] = '\0';
        } else {
            for (i = 8; *name != '\0' && i < 11; i++) {
                sfn[i] = toupper(*name++);
            }
            if (*name != '\0') {
                return TF_ERR_FNAME_IVALID;
            }
        }
    }
    return 0;
}

int tf_sfn2name(const char* sfn, char* name)
{
    for (uint8_t i = 0; i < 8 && sfn[i] != ' '; i++) {
        *name++ = tolower(sfn[i]);
    }
    if (sfn[8] == ' ') {
        *name = '\0';
    } else {
        *name++ = '.';
        for (uint8_t i = 8; i < 11 && sfn[i] != ' '; i++) {
            *name++ = tolower(sfn[i]);
        }
        *name = '\0';
    }
    return 0;
}
