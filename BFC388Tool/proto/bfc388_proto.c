\
#include "bfc388_proto.h"
#include "serial_port_win.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

// Packet format: 0xEF 0xAA | MsgID(1) | Size(2) | Data(N) | XOR(1)
// XOR = XOR of all bytes except the 2-byte SyncWord

#define MAX_CMD_LEN		256

#define SYNC_H 0xEF
#define SYNC_L 0xAA

static fc_on_reply   g_on_reply   = NULL;
static fc_on_note    g_on_note    = NULL;
static fc_on_image   g_on_image   = NULL;
static void*         g_user       = NULL;

static HANDLE g_rxThread = NULL;
static volatile LONG g_rxStop = 0;

static uint8_t calc_xor(const uint8_t* buf, uint32_t size)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < size; ++i) x ^= buf[i];
    return x;
}

void bfc388_set_callbacks(fc_on_reply r, fc_on_note n, fc_on_image i, void* user)
{
    g_on_reply = r; g_on_note = n; g_on_image = i; g_user = user;
}

static DWORD WINAPI rx_thread_proc(LPVOID lp)
{
    (void)lp;
    enum { S_SYNC1, S_SYNC2, S_MSGID, S_SIZE1, S_SIZE2, S_DATA, S_CHECK } state = S_SYNC1;
    uint8_t msgid = 0;
    uint16_t size = 0;
    uint16_t got = 0;
    uint8_t* data = NULL;
    uint8_t chk = 0, calc = 0;

    for (;;)
    {
        if (InterlockedCompareExchange(&g_rxStop, 0, 0)) break;
        uint8_t b = 0;
        int r = sp_read(&b, 1, 50); // 50ms timeout
        if (r <= 0) continue;
        switch (state)
        {
        case S_SYNC1:
            if (b == SYNC_H) state = S_SYNC2;
            break;
        case S_SYNC2:
            if (b == SYNC_L) state = S_MSGID;
            else state = S_SYNC1;
            break;
        case S_MSGID:
            msgid = b;
            state = S_SIZE1;
            break;
        case S_SIZE1:
            size = (uint16_t)b;
            state = S_SIZE2;
            break;
        case S_SIZE2:
            size |= ((uint16_t)b << 8);
            if (size > 0) {
                data = (uint8_t*)malloc(size);
                got = 0;
                state = S_DATA;
            } else {
                state = S_CHECK;
            }
            break;
        case S_DATA:
            data[got++] = b;
            if (got >= size) state = S_CHECK;
            break;
        case S_CHECK:
            chk = b;
            // reconstruct check region: MsgID + Size(2) + Data(N)
            {
                uint8_t* buf = (uint8_t*)malloc(1 + 2 + size);
                buf[0] = msgid;
                buf[1] = (uint8_t)(size & 0xFF);
                buf[2] = (uint8_t)(size >> 8);
                if (size) memcpy(buf + 3, data, size);
                calc = calc_xor(buf, 1 + 2 + size);
                free(buf);
            }
            if (calc == chk) {
                // Dispatch
                if (msgid == MID_REPLY) {
                    // data: mid(1) result(1) payload...
                    if (size >= 2 && g_on_reply) {
                        uint8_t mid = data[0], result = data[1];
                        const uint8_t* payload = (size > 2) ? (data + 2) : NULL;
                        uint16_t psz = (size > 2) ? (size - 2) : 0;
                        g_on_reply(mid, result, payload, psz, g_user);
                    }
                } else if (msgid == MID_NOTE) {
                    if (size >= 1 && g_on_note) {
                        uint8_t nid = data[0];
                        const uint8_t* payload = (size > 1) ? (data + 1) : NULL;
                        uint16_t psz = (size > 1) ? (size - 1) : 0;
                        g_on_note(nid, payload, psz, g_user);
                    }
                } else if (msgid == MID_IMAGE) {
                    if (g_on_image && size>0) {
                        g_on_image(data, size, g_user);
                        // g_on_image takes ownership copy in UI; free here after dispatch below
                    }
                }
            }
            if (data) free(data);
            data = NULL; size = 0; got = 0;
            state = S_SYNC1;
            break;
        }
    }
    return 0;
}

int bfc388_open(const wchar_t* com_port, int baud)
{
    if (sp_open(com_port, baud) != 0) return -1;
    InterlockedExchange(&g_rxStop, 0);
    DWORD tid = 0;
    g_rxThread = CreateThread(NULL, 0, rx_thread_proc, NULL, 0, &tid);
    return 0;
}

void bfc388_close(void)
{
    InterlockedExchange(&g_rxStop, 1);
    if (g_rxThread) {
        WaitForSingleObject(g_rxThread, 500);
        CloseHandle(g_rxThread);
        g_rxThread = NULL;
    }
    sp_close();
}

int bfc388_send(uint8_t mid, const uint8_t* data, uint16_t size)
{
	uint8_t msgBuf[MAX_CMD_LEN] = {0};

	if (size + 6 > MAX_CMD_LEN)
	{
		return -1;
	}

	msgBuf[0] = SYNC_H;
	msgBuf[1] = SYNC_L;
	msgBuf[2] = mid;
    msgBuf[3] = (uint8_t)(size & 0xFF);
    msgBuf[4] = (uint8_t)(size >> 8);
	if (size > 0)
	{
		memcpy(msgBuf + 5, data, size);
	}

	msgBuf[5 + size] = calc_xor(msgBuf + 2, size + 3);
	
    int r = sp_write(msgBuf, size + 6);
	if (r != size + 6)
	{
		return -1;
	}

    return 0;
}

// ------------ Command wrappers --------------

void bfc388_reset(void)
{
    bfc388_send(MID_RESET, NULL, 0);
}

void bfc388_get_status(void)
{
    bfc388_send(MID_GETSTATUS, NULL, 0);
}

void bfc388_verify(uint8_t pd_rightaway, uint8_t timeout, uint8_t verify_mode)
{
    uint8_t d[3] = { pd_rightaway, timeout, verify_mode };
    bfc388_send(MID_VERIFY, d, sizeof(d));
}

void bfc388_enroll(uint8_t admin, const wchar_t* user_name, uint8_t face_dir, uint8_t timeout)
{
    uint8_t d[1+32+1+1] = {0};
    d[0] = admin ? 1 : 0;
    // copy user_name as UTF-8-ish: here we simply truncate wchar to char (ASCII expected)
    for (int i=0;i<32;i++) {
        wchar_t wc = user_name ? user_name[i] : 0;
        if (!wc) break;
        d[1+i] = (uint8_t)(wc & 0xFF);
    }
    d[1+32] = face_dir;
    d[1+32+1] = timeout;
    bfc388_send(MID_ENROLL, d, sizeof(d));
}

void bfc388_enroll_single(uint8_t admin, const wchar_t* user_name, uint8_t face_dir, uint8_t timeout)
{
    uint8_t d[1+32+1+1] = {0};
    d[0] = admin ? 1 : 0;
    for (int i=0;i<32;i++) {
        wchar_t wc = user_name ? user_name[i] : 0;
        if (!wc) break;
        d[1+i] = (uint8_t)(wc & 0xFF);
    }
    d[1+32] = face_dir; // ignored by module per doc
    d[1+32+1] = timeout;
    bfc388_send(MID_ENROLL_SINGLE, d, sizeof(d));
}

void bfc388_delete_user(uint16_t user_id, uint8_t user_type)
{
    uint8_t d[3] = { (uint8_t)(user_id>>8), (uint8_t)(user_id&0xFF), user_type };
    bfc388_send(MID_DELUSER, d, sizeof(d));
}

void bfc388_delete_all(uint8_t type, uint16_t begin_id, uint16_t end_id)
{
    uint8_t d[5] = { type, (uint8_t)(begin_id>>8), (uint8_t)(begin_id&0xFF), (uint8_t)(end_id>>8), (uint8_t)(end_id&0xFF) };
    bfc388_send(MID_DELALL, d, sizeof(d));
}

void bfc388_get_all_userid(uint8_t fmt)
{
    bfc388_send(MID_GET_ALL_USERID, NULL, 0);
}

void bfc388_snap_image(uint8_t image_counts, uint8_t start_number)
{
    uint8_t d[2] = { image_counts, start_number };
    bfc388_send(MID_SNAPIMAGE, d, sizeof(d));
}

void bfc388_get_saved_image_size(uint8_t image_number)
{
    uint8_t d[1] = { image_number };
    bfc388_send(MID_GETSAVEDIMAGE, d, sizeof(d));
}

void bfc388_upload_image(uint32_t offset, uint32_t size)
{
    uint8_t d[8];
    d[0] = (uint8_t)((offset>>24)&0xFF);
    d[1] = (uint8_t)((offset>>16)&0xFF);
    d[2] = (uint8_t)((offset>>8)&0xFF);
    d[3] = (uint8_t)((offset)&0xFF);
    d[4] = (uint8_t)((size>>24)&0xFF);
    d[5] = (uint8_t)((size>>16)&0xFF);
    d[6] = (uint8_t)((size>>8)&0xFF);
    d[7] = (uint8_t)((size)&0xFF);
    bfc388_send(MID_UPLOADIMAGE, d, sizeof(d));
}

void bfc388_download_user_char(uint8_t char_type, uint32_t offset, uint32_t size, const uint8_t* data)
{
    uint8_t* d = (uint8_t*)malloc(1 + 4 + 4 + size);
    d[0] = char_type;
    d[1] = (uint8_t)((offset>>24)&0xFF);
    d[2] = (uint8_t)((offset>>16)&0xFF);
    d[3] = (uint8_t)((offset>>8)&0xFF);
    d[4] = (uint8_t)((offset)&0xFF);
    d[5] = (uint8_t)((size>>24)&0xFF);
    d[6] = (uint8_t)((size>>16)&0xFF);
    d[7] = (uint8_t)((size>>8)&0xFF);
    d[8] = (uint8_t)((size)&0xFF);
    if (size) memcpy(d+9, data, size);
    bfc388_send(MID_DOWNLOADUSERCHAR, d, (uint16_t)(1+4+4+size));
    free(d);
}

void bfc388_save_download_user_char(uint8_t char_type, uint16_t save_id)
{
    uint8_t d[1+2];
    d[0] = char_type;
    d[1] = (uint8_t)(save_id>>8);
    d[2] = (uint8_t)(save_id&0xFF);
    bfc388_send(MID_SAVEDOWNLOADUSERCHAR, d, sizeof(d));
}

void bfc388_upload_user_char(uint8_t char_type, uint16_t id, uint32_t offset, uint32_t size)
{
    uint8_t d[1 + 2 + 4 + 4];
    d[0] = char_type;
    d[1] = (uint8_t)(id>>8);
    d[2] = (uint8_t)(id&0xFF);
    d[3] = (uint8_t)((offset>>24)&0xFF);
    d[4] = (uint8_t)((offset>>16)&0xFF);
    d[5] = (uint8_t)((offset>>8)&0xFF);
    d[6] = (uint8_t)((offset)&0xFF);
    d[7] = (uint8_t)((size>>24)&0xFF);
    d[8] = (uint8_t)((size>>16)&0xFF);
    d[9] = (uint8_t)((size>>8)&0xFF);
    d[10] = (uint8_t)((size)&0xFF);
    bfc388_send(MID_UPLOADUSERCHAR, d, sizeof(d));
}

void bfc388_stop_receiver(void)
{
    // signal thread to stop, but keep port open
    InterlockedExchange(&g_rxStop, 1);
    if (g_rxThread) {
        WaitForSingleObject(g_rxThread, 500);
        CloseHandle(g_rxThread);
        g_rxThread = NULL;
    }
}
