# Serial Usage and Runtime Behavior

[English README](../README.md) | [中文 README](../README.zh-CN.md)

LILYGO Serial accesses serial devices attached to the machine running the app.
The SDL2 simulator uses the same POSIX backend as the device build and can
communicate with real local ports. A connected device, its operating-system
driver, and read/write permission are required. Serial operation requires no
network connection.

## Connect a Device

1. Attach the serial adapter or board, then launch the application. Click **Scan**
   if the device was attached after startup.
2. Select the device path and the settings required by the other endpoint.
   The defaults are **115200 baud, 8 data bits, no parity, 1 stop bit, no flow
   control** (`115200 8N1`).
3. Click **Connect**. A successful connection changes the badge to **Connected**
   and enables sending and the available line controls.
4. Choose the receive display format and compose a message in the send panel.
   Select its input format and line ending, then click **Send**.
5. Click **Disconnect** before changing the device or serial parameters.

The send field opens an on-screen keyboard; its ready/cancel action hides the
keyboard. Sending keeps the entered text available for reuse. Portrait and
compact layouts may require scrolling to reach the send panel.

### Device Discovery

The application scans the direct children of `/dev` at startup and when **Scan**
is clicked. It accepts character devices, including symlinks resolving to
character devices, with these names:

| Platform / family | Accepted names |
| --- | --- |
| macOS callout devices | `cu.` followed by a nonempty suffix, such as `cu.usbserial-...` |
| Linux USB / CDC devices | `ttyUSB` or `ttyACM` followed by digits |
| Linux onboard UARTs | `ttyAMA`, `ttyS`, or `ttyTHS` followed by digits |
| Serial aliases | `serial` followed by digits |

The exact paths `/dev/ttyS0` and `/dev/serial0` are intentionally hidden from
the picker. Discovery does not recursively scan `/dev/serial/by-id` or enumerate
every possible driver naming scheme. The UI does not offer a custom-path field;
the internal `ViewModel::select_path()` method is used by tests and integrations.
The discovery filter is not an operating-system access restriction.

Hot-plug discovery and reconnection are manual. If a connection drops, check the
device and cable, scan again if necessary, and reconnect. Scanning preserves the
selected path when possible and keeps a connected visible endpoint selectable
even if it temporarily disappears from discovery.

### Serial Settings

| Setting | Choices |
| --- | --- |
| Baud rate | `110`, `300`, `600`, `1200`, `2400`, `4800`, `9600`, `19200`, `38400`, `57600`, `115200`, `230400` |
| Additional baud rates | `460800`, `500000`, `576000`, `921600`, `1000000`, `1500000`, `2000000`, `3000000`, when defined by the build platform's termios headers |
| Data bits | `5`, `6`, `7`, `8` |
| Parity | None, odd, even |
| Stop bits | `1`, `2` |
| Flow control | None, RTS/CTS, XON/XOFF |

The dropdown is populated from the platform-supported baud constants; arbitrary
custom rates are not available. Driver and hardware support can further limit
valid combinations. The backend applies raw mode and reads back the baud rate
and principal frame settings. A rejected or mismatched configuration prevents
connection instead of silently substituting another setting.

RTS/CTS requires platform and driver support. XON/XOFF uses `0x11` for start and
`0x13` for stop, so those bytes participate in terminal flow control when enabled.
Use the same flow-control scheme as the device.

## Receive Data

The **Receive** panel displays only bytes read from the device. Sent bytes appear
in the TX counter; the application does not insert a local echo into Receive.
A device may itself echo received data back, in which case that echo appears as
RX data.

- **ASCII:** prints bytes `0x20`–`0x7E`, LF, and tab directly; renders CR as `\r`
  and other bytes as uppercase `\xNN` escapes. Non-ASCII bytes are not decoded
  as UTF-8 text.
- **HEX:** shows uppercase byte pairs separated by spaces, such as `41 00 FF`.
- **Time:** adds a local timestamp in `[HH:MM:SS.mmm]` format to each receive
  record. It is enabled by default.
- **Auto:** scrolls to the latest data. Turn it off to inspect earlier data
  without each update moving the scroll position. It is enabled by default.
- **Clear:** removes retained records and resets both byte counters.

Receive records follow operating-system read chunks, not protocol messages or
lines. Their timestamps describe when the application recorded the chunk. The
display preserves payload line feeds and avoids adding a redundant final blank
row. Switching ASCII/HEX or timestamps reformats the retained raw data.

## Send Data

Receive and send formats are independent. ASCII is the default for both.

| Input mode | Accepted input | Example |
| --- | --- | --- |
| ASCII | Characters whose byte values are `0x00`–`0x7F` | `AT` sends `41 54` before the selected ending |
| HEX | Complete byte pairs, contiguous or separated by ASCII whitespace | `0041ff` and `00 41 FF` both send `00 41 FF` |

HEX does not accept `0x` prefixes, commas, single hex digits, or spaces inside a
byte pair. ASCII does not interpret escape syntax: typing `\r` sends a backslash
and `r`. Use the line-ending selector or HEX input for control bytes. Non-ASCII
text is rejected before transmission; enter its desired byte encoding in HEX
if needed.

The line-ending selector applies to **both** input modes:

| Ending | Appended bytes |
| --- | --- |
| No ending (default) | None |
| LF | `0A` |
| CR | `0D` |
| CRLF | `0D 0A` |

For example, ASCII `AT` with CRLF sends `41 54 0D 0A`. Choose **No ending** when
the HEX message already includes its terminator. An empty message with a selected
ending sends that ending alone; empty input with **No ending** is rejected.

Validation completes before queuing any bytes. The backend attempts an immediate
nonblocking write and keeps any unwritten bytes for later polling. **Data sent**
and the TX counter mean that the operating system accepted the bytes; they do
not confirm physical transmission or acknowledgment by the other device.

## RTS, DTR, and BREAK

These switches require an active connection and a driver implementing the
corresponding modem-control operations.

- **RTS / DTR:** request an asserted or released output line. After connecting,
  the application attempts to release both lines and reads back their states
  when the driver supports it. RTS is controlled by the driver and its manual
  switch is disabled when RTS/CTS flow control is selected.
- **BREAK:** holds a break condition until switched off or the connection is
  closed. It is not a timed pulse command.
- **Unsupported operations:** show an error and retain the last known state.
  An adapter can still support data transfer without supporting these controls.

The app polls readable RTS/DTR states while connected. A switch is not an
electrical measurement, and it cannot verify an adapter's physical pins. On
disconnect, the backend attempts to release its BREAK and restore the original
terminal settings and readable original modem state before closing the port.

## Permissions and Troubleshooting

The `serial` permission in `lpm.toml` declares the application's purpose. It does
not grant Unix device access or install a udev rule. The user running the
simulator or launcher must have permission to read and write the selected node.

On Linux, inspect the actual device and your group membership, substituting the
path selected in the application:

```sh
ls -l /dev/ttyUSB0
id
```

On Debian / Raspberry Pi OS, serial devices commonly belong to `dialout`. If the
node uses that group, add your account and sign out and back in before retrying:

```sh
sudo usermod -aG dialout "$USER"
```

Use the group or udev policy appropriate to the target system. On macOS, check
that the adapter driver creates a `/dev/cu.*` device and that the current user
can access it. Close other serial monitors before connecting; the app does not
request exclusive ownership of a device.

| Symptom | Check |
| --- | --- |
| No ports found | Adapter connection, driver, supported device naming, intentional hidden paths, then **Scan**. |
| Cannot open / permission denied | Device path, node ownership and permissions, and the account running the app. |
| Cannot apply settings | Adapter-supported baud rate, data bits, parity, stop bits, and flow control. |
| Connected but no readable data | Wiring, matching settings, device output, flow control, and ASCII versus HEX display. |
| TX increases but Receive stays empty | TX is not locally echoed; verify that the device sends a response or provides loopback. |
| Output queue full | Wait for queued bytes to drain and check flow-control wiring or XON/XOFF state. |
| Line control fails | Platform, adapter, or driver support; manual RTS is unavailable under RTS/CTS. |
| Port disconnected | Cable, device power, and driver state; reconnect manually. |

## Limits and Validation Scope

The UI input is limited to 16,384 characters. A parsed message including its
ending and the total pending output queue are each limited to 64 KiB. A message
that does not fit in the queue is rejected in full. Disconnecting drops pending
output; **Clear** keeps the connection and pending output and does not flush
driver buffers.

The in-memory RX/TX history retains at most 256 KiB of raw bytes and 512 records.
The oldest records are discarded first. The Receive display is capped at 32 KiB
of formatted text, with an omission marker when formatting truncates retained
data. These limits make it a live debugging console, not a complete capture log.
Byte counters continue across history eviction and reconnection until **Clear**.
The app does not save settings, logs, or input across application restarts and
does not provide log export, periodic sending, protocol decoding, or automatic
reconnection.

Automated tests use local pseudoterminals to cover parsing, data transfer,
partial writes, capacity limits, state publication, error handling, terminal
restoration, and UI interactions. They do not establish real-adapter behavior
for every baud rate, parity/data-bit combination, flow-control mode, or physical
RTS/DTR/BREAK signal. Verify those on the target hardware. See the
[architecture overview](00-overview.md) and
[UI guidelines](01-user-interface.md) for implementation and acceptance details.
