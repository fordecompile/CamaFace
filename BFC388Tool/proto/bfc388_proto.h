#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <wchar.h>



// Message IDs (subset used)
enum {
    MID_REPLY          = 0x00,
    MID_NOTE           = 0x01,
    MID_IMAGE          = 0x02,

    MID_RESET          = 0x10,
    MID_GETSTATUS      = 0x11,
    MID_VERIFY         = 0x12,
    MID_ENROLL         = 0x13,
    MID_SNAPIMAGE      = 0x16,
    MID_GETSAVEDIMAGE  = 0x17,
    MID_UPLOADIMAGE    = 0x18,
    MID_ENROLL_SINGLE  = 0x1D,
    MID_DELUSER        = 0x20,
    MID_DELALL         = 0x21,
    MID_GETUSERINFO    = 0x22,
    MID_GET_ALL_USERID = 0x24,
    MID_ENROLL_ITG     = 0x26,
    MID_GET_VERSION    = 0x30,

    MID_CAPTURE_PIC_TYPE = 0x9A,

    MID_DOWNLOADUSERCHAR = 0xA0,
    MID_SAVEDOWNLOADUSERCHAR = 0xA1,
    MID_UPLOADUSERCHAR  = 0xA2,
};

// Results (Table 6)
enum {
    MR_SUCCESS = 0,
    MR_REJECTED = 1,
    MR_ABORTED = 2,
    MR_FAILED4_CAMERA = 4,
    MR_FAILED4_UNKNOWNREASON = 5,
    MR_FAILED4_INVALIDPARAM = 6,
    MR_FAILED4_NOMEMORY = 7,
    MR_FAILED4_UNKNOWNUSER = 8,
    MR_FAILED4_MAXUSER = 9,
    MR_FAILED4_FACEENROLLED = 10,
    MR_FAILED4_LIVENESSCHECK = 12,
    MR_FAILED4_TIMEOUT = 13,
    MR_FAILED4_AUTHORIZATION = 14,
    MR_FAILED4_READ_FILE = 19,
    MR_FAILED4_WRITE_FILE = 20,
    MR_FAILED4_NO_ENCRYPT = 21,
};

// NOTE ids (Table 8)
enum {
    CAMA_NID_READY = 0,
    CAMA_NID_FACE_STATE = 1,
};

// Face direction bits (Table 14)
enum {
    FACE_DIRECTION_UP      = 0x10,
    FACE_DIRECTION_DOWN    = 0x08,
    FACE_DIRECTION_LEFT    = 0x04,
    FACE_DIRECTION_RIGHT   = 0x02,
    FACE_DIRECTION_MIDDLE  = 0x01,
    FACE_DIRECTION_UNDEFINE= 0x00,
};

// Callback prototypes
typedef void (*fc_on_reply)(uint8_t mid, uint8_t result, const uint8_t* data, uint16_t size, void* user);
typedef void (*fc_on_note)(uint8_t nid, const uint8_t* data, uint16_t size, void* user);
typedef void (*fc_on_image)(const uint8_t* data, uint16_t size, void* user);

// API
void bfc388_set_callbacks(fc_on_reply r, fc_on_note n, fc_on_image i, void* user);
int  bfc388_open(const wchar_t* com_port, int baud);
void bfc388_close(void);

// Generic send
int  bfc388_send(uint8_t mid, const uint8_t* data, uint16_t size);

// High-level wrappers (C only, no C++)
void bfc388_reset(void);
void bfc388_get_status(void);
void bfc388_verify(uint8_t pd_rightaway, uint8_t timeout, uint8_t verify_mode);
void bfc388_enroll(uint8_t admin, const wchar_t* user_name, uint8_t face_dir, uint8_t timeout);
void bfc388_enroll_single(uint8_t admin, const wchar_t* user_name, uint8_t face_dir, uint8_t timeout);
void bfc388_delete_user(uint16_t user_id, uint8_t user_type);
void bfc388_delete_all(uint8_t type, uint16_t begin_id, uint16_t end_id);
void bfc388_get_all_userid(uint8_t fmt);

void bfc388_snap_image(uint8_t image_counts, uint8_t start_number);
void bfc388_get_saved_image_size(uint8_t image_number);
void bfc388_upload_image(uint32_t offset, uint32_t size);

void bfc388_download_user_char(uint8_t char_type, uint32_t offset, uint32_t size, const uint8_t* data);
void bfc388_save_download_user_char(uint8_t char_type, uint16_t save_id);
void bfc388_upload_user_char(uint8_t char_type, uint16_t id, uint32_t offset, uint32_t size);

// Internal receive pump (started automatically on open)
void bfc388_stop_receiver(void);

#ifdef __cplusplus
}
#endif
