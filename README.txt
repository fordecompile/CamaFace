# BFC388 Face Tool (VS2017 MFC)

This is a dialog-based MFC upper-computer for BFC388 face module over UART.

- UI: C++/MFC
- Module interactions & serial protocol: standard C (in `proto/`)
- Encryption and palm-vein are intentionally not implemented (plain communication).

## Build (VS2017)

1. Open `BFC388Tool.sln` in Visual Studio 2017.
2. Ensure you have the *MFC* and *Windows 10 SDK* installed.
3. Build **x86** or **x64** (Debug/Release). MFC is set to **Use MFC in a Shared DLL**.
4. Run. The app will enumerate COM ports using SetupAPI.

## Usage

1. Select **COM Port** and **Baud** (default 115200 as per spec).
2. Click **Open**.
3. Use the buttons for enroll, verify, delete, list IDs, snap image, upload/download templates, etc.
4. Messages from `NOTE` and `REPLY` are shown in the **Log** area.
5. The **Image** area shows a JPEG captured via `MID_SNAPIMAGE` + `MID_GETSAVEDIMAGE` + `MID_UPLOADIMAGE`.

## Implementation Notes

- Packet format follows the spec: Sync `0xEF,0xAA`, then MsgID, Size, Data, and XOR parity of all bytes except the 2-byte Sync.
- `NOTE` with `NID_FACE_STATE` shows hints like "Too far / Too close / Occlusion / Wrong direction" with yaw/pitch/roll.
- 5-direction enroll is implemented by sending `MID_ENROLL` repeatedly with directions in the order: Middle → Up → Down → Left → Right, until the returned `face_direction` bitmask covers all five directions.
- Single-frame enroll uses `MID_ENROLL_SINGLE` (no direction interaction needed).
- Snap image uses `MID_SNAPIMAGE` + `MID_GETSAVEDIMAGE`, then requests chunks via `MID_UPLOADIMAGE` (up to 4000 bytes each). The module returns image chunks as `MID_IMAGE` frames, which are assembled and rendered with GDI+.
- List IDs uses `MID_GET_ALL_USERID` with long format (fmt=0).

> All message IDs, note IDs, state codes, and direction bits are taken from the provided protocol PDF.

