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

	// Fixed 4KB buffer first, expand with malloc only when a single message is larger than 4KB.
	static uint8_t fixed_buf[4096];
	uint8_t* buf = fixed_buf;
	size_t   cap = sizeof(fixed_buf);
	size_t   have = 0;
	int      using_heap = 0;

	for (;;)
	{
		if (InterlockedCompareExchange(&g_rxStop, 0, 0)) break;

		// 1) First read: try to read up to 4096 bytes at once.
		//    If < 5 bytes (EF AA + MsgID + SizeL + SizeH not fully available), keep reading until >=5 or timeout.
		if (have < 5) {
			int r = sp_read(buf + have, (int)(cap - have), 500); // try read as much as possible (4KB initially)
			if (r > 0) {
				have += (size_t)r;
			}
			else {
				// timeout or error -> next loop / check stop
				continue;
			}
			if (have < 5) {
				// still not enough header (need at least 5 bytes to know total length)
				continue;
			}
		}

		// 2) Find sync header 0xEF 0xAA in the buffered data; discard leading noise if any.
		//    Keep at least one byte if the last is SYNC_H to handle boundary case.
		for (;;) {
			if (have >= 2 && buf[0] == SYNC_H && buf[1] == SYNC_L) break;
			if (have == 0) break;
			// slide forward to next potential sync
			size_t shift = 1;
			// try to skip fast by searching next 0xEF
			size_t i = 0;
			for (i = 0; i + 1 < have; ++i) {
				if (buf[i] == SYNC_H && buf[i + 1] == SYNC_L) { shift = i; break; }
			}
			if (i + 1 >= have) shift = have >= 1 ? (have - 1) : 1; // keep last byte if it's possible part of sync
			if (shift > 0) {
				memmove(buf, buf + shift, have - shift);
				have -= shift;
			}
			if (have < 2) {
				// need more to check sync
				int r = sp_read(buf + have, (int)(cap - have), 50);
				if (r > 0) have += (size_t)r;
				else break; // timeout; go outer loop
			}
		}
		if (have < 5) continue; // need full header (EF AA + id + sizeL + sizeH)

		// 3) We have at least header, compute total frame length: 2(sync)+1(id)+2(size)+N(data)+1(chk)
		uint8_t  msgid = buf[2];
		uint16_t size = (((uint16_t)buf[3]) << 8) | ((uint16_t)buf[4]);
		size_t   total = 6 + (size_t)size;

		// 4) If the current message is larger than 4KB buffer, allocate a bigger one and copy existing bytes.
		if (total > cap) {
			uint8_t* nb = (uint8_t*)malloc(total);
			if (!nb) {
				// OOM: drop current bytes and resync
				have = 0;
				if (using_heap) { free(buf); buf = fixed_buf; cap = sizeof(fixed_buf); using_heap = 0; }
				continue;
			}
			memcpy(nb, buf, have);
			if (using_heap) free(buf);
			buf = nb; cap = total; using_heap = 1;
		}

		// 5) If we still don't have a full message, keep reading until full message is received or timeout.
		if (have < total) {
			int r = sp_read(buf + have, (int)(total - have), 50);
			if (r > 0) {
				have += (size_t)r;
				if (have < total) continue; // still not enough
			}
			else {
				continue; // timeout or error -> try again next loop
			}
		}

		if (have < total) continue; // safety

		// 6) Now we have a complete message in buf[0..total-1], verify XOR.
		uint8_t chk = buf[5 + size];
		uint8_t calc = calc_xor(buf + 2, (uint32_t)(3 + size)); // MsgID + Size(2) + Data(N)

		if (calc == chk) {
			// Dispatch just like the old state machine did. Data points to buf+5 with length 'size'.
			const uint8_t* data = buf + 5;

			if (msgid == MID_REPLY) {
				if (size >= 2 && g_on_reply) {
					uint8_t mid = data[0], result = data[1];
					const uint8_t* payload = (size > 2) ? (data + 2) : NULL;
					uint16_t psz = (size > 2) ? (uint16_t)(size - 2) : 0;
					g_on_reply(mid, result, payload, psz, g_user);
				}
			}
			else if (msgid == MID_NOTE) {
				if (size >= 1 && g_on_note) {
					uint8_t nid = data[0];
					const uint8_t* payload = (size > 1) ? (data + 1) : NULL;
					uint16_t psz = (size > 1) ? (uint16_t)(size - 1) : 0;
					g_on_note(nid, payload, psz, g_user);
				}
			}
			else if (msgid == MID_IMAGE) {
				if (g_on_image && size > 0) {
					g_on_image(data, (uint16_t)size, g_user);
				}
			}
		}

		// 7) If buffer contains more than one message, move leftover to the front for next loop.
		if (have > total) {
			size_t remain = have - total;
			memmove(buf, buf + total, remain);
			have = remain;

			// If we were using heap but the remaining data fits in 4KB, shrink back to fixed buffer.
			if (using_heap && cap > sizeof(fixed_buf) && remain <= sizeof(fixed_buf)) {
				memcpy(fixed_buf, buf, remain);
				free(buf);
				buf = fixed_buf;
				cap = sizeof(fixed_buf);
				using_heap = 0;
			}
		}
		else {
			// exact or consumed all
			have = 0;
			if (using_heap) { free(buf); buf = fixed_buf; cap = sizeof(fixed_buf); using_heap = 0; }
		}
	}
	if (using_heap) free(buf);
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
