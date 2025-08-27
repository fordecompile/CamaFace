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
    // Adjust timeouts for this call
    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout = timeout_ms;
    to.ReadTotalTimeoutConstant = timeout_ms;
    to.ReadTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 200;
    SetCommTimeouts(g_hCom, &to);
    DWORD rd = 0;
    if (!ReadFile(g_hCom, data, len, &rd, NULL)) return -2;
    return (int)rd;
}
