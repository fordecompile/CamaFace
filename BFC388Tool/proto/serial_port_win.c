#include "serial_port_win.h"
#include <windows.h>

static HANDLE g_hCom = INVALID_HANDLE_VALUE;

static int baud_to_flag(int baud)
{
    switch (baud) {
    case 9600: return CBR_9600;
    case 19200: return CBR_19200;
    case 38400: return CBR_38400;
    case 57600: return CBR_57600;
    case 115200: return CBR_115200;
    case 230400: return 230400;
    case 460800: return 460800;
    case 921600: return 921600;
    default: return baud;
    }
}

int sp_open(const wchar_t* com_port, int baud)
{
    if (g_hCom != INVALID_HANDLE_VALUE) sp_close();

    wchar_t path[64] = L"";
    // For COM numbers > 9, need \\.\COMx
    wsprintf(path, L"\\\\.\\%s", com_port);
    g_hCom = CreateFileW(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_hCom == INVALID_HANDLE_VALUE) return -1;

    // Configure
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(g_hCom, &dcb)) { sp_close(); return -2; }
    dcb.BaudRate = baud_to_flag(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE; dcb.fInX = FALSE;
    if (!SetCommState(g_hCom, &dcb)) { sp_close(); return -3; }

    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout = 50;
    to.ReadTotalTimeoutConstant = 50;
    to.ReadTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 200;
    to.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(g_hCom, &to);
    SetupComm(g_hCom, 4096, 4096);
    PurgeComm(g_hCom, PURGE_RXCLEAR|PURGE_TXCLEAR);

    return 0;
}

void sp_close(void)
{
    if (g_hCom != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hCom);
        g_hCom = INVALID_HANDLE_VALUE;
    }
}

int sp_write(const void* data, int len)
{
    if (g_hCom == INVALID_HANDLE_VALUE) return -1;
    DWORD wr = 0;
    if (!WriteFile(g_hCom, data, len, &wr, NULL)) return -2;
    return (int)wr;
}

int sp_read(void* data, int len, int timeout_ms)
{
	if (g_hCom == INVALID_HANDLE_VALUE) return -1;

	// 先检查是否已经有可读数据；若有，则立刻以“非阻塞”读取并返回。
	DWORD errs = 0;
	COMSTAT st = { 0 };
	ClearCommError(g_hCom, &errs, &st);
	if (st.cbInQue > 0) {
		DWORD want = (DWORD)len;
		if (want > st.cbInQue) want = st.cbInQue;

		COMMTIMEOUTS to_now = { 0 };
		// 这组配置使 ReadFile 立即返回已有的数据（非阻塞）
		to_now.ReadIntervalTimeout = MAXDWORD;
		to_now.ReadTotalTimeoutMultiplier = 0;
		to_now.ReadTotalTimeoutConstant = 0;
		to_now.WriteTotalTimeoutConstant = 200;
		SetCommTimeouts(g_hCom, &to_now);

		DWORD rd = 0;
		if (!ReadFile(g_hCom, data, want, &rd, NULL)) return -2;
		return (int)rd; // 立刻返回当前可读字节数
	}

	// 走到这里说明当前无可读数据：进入“等待首字节到达”的模式
	COMMTIMEOUTS to_wait = { 0 };
	to_wait.ReadIntervalTimeout = MAXDWORD;
	to_wait.ReadTotalTimeoutMultiplier = 0;
	to_wait.ReadTotalTimeoutConstant = (timeout_ms < 0 ? 0 : (DWORD)timeout_ms);
	to_wait.WriteTotalTimeoutConstant = 200;
	SetCommTimeouts(g_hCom, &to_wait);

	DWORD rd = 0;
	if (!ReadFile(g_hCom, data, len, &rd, NULL)) return -2;
	if (rd == 0) {
		// 超时且仍无数据
		return 0;
	}

	int total = rd;

	ClearCommError(g_hCom, &errs, &st);
	if (st.cbInQue > 0 && total < len) {
		DWORD want2 = st.cbInQue;
		DWORD room = (DWORD)(len - total);
		if (want2 > room) want2 = room;

		COMMTIMEOUTS to_now2 = { 0 };
		to_now2.ReadIntervalTimeout = MAXDWORD;
		to_now2.ReadTotalTimeoutMultiplier = 0;
		to_now2.ReadTotalTimeoutConstant = 0;
		to_now2.WriteTotalTimeoutConstant = 200;
		SetCommTimeouts(g_hCom, &to_now2);

		DWORD rd2 = 0;
		if (want2 > 0) {
			ReadFile(g_hCom, ((BYTE*)data) + total, want2, &rd2, NULL);
			total += (int)rd2;
		}
	}

	return total;
}

