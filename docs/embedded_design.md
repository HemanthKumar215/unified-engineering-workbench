# Task E: Embedded-system integration design

**Scope:** a modular, host-connected bench with **one microcontroller board, one FPGA board and one JTAG debug adapter**. The software side (device adapters and UI) is implemented with **simulated** devices. Physical wiring, firmware and FPGA programming are out of scope.

Block diagram: [`src/frontend/block_diagram.svg`](../src/frontend/block_diagram.svg). It is also shown in the Hardware view of the app.

## 1. Device-specific assumptions

No common connector, voltage or protocol is assumed. Each row below is an explicit assumption that must be checked against the actual board's schematic before wiring.

| Device | Assumed class | Host link | Power | I/O voltage | Debug / config connector |
|---|---|---|---|---|---|
| MCU board | Arm Cortex-M4 dev board (e.g. Nucleo/Discovery class) | USB CDC-ACM virtual COM port | 5 V from USB → on-board 3.3 V LDO | 3.3 V GPIO/UART/SPI; most pins **not** 5 V tolerant | 10-pin 1.27 mm Cortex debug (SWD/JTAG), VTREF = 3.3 V |
| FPGA board | Xilinx Artix-7 dev board (e.g. XC7A35T class) | USB-UART bridge (status/telemetry) | External 5–12 V barrel or USB, on-board regulators | VCCINT 1.0 V, VCCAUX 1.8 V; I/O banks set by VCCO (3.3 V **or** 1.8 V per bank) | 14-pin 2 mm Xilinx JTAG; TAP is powered from VCCO_0 (usually 3.3 V) |
| JTAG adapter | FT2232H-class probe driven by OpenOCD | USB 2.0 High-Speed | Powered by USB; **does not power the target** | Output buffers follow the target's VTREF (1.8–3.3 V) | 20-pin 2.54 mm ARM JTAG, plus adapters to 10-pin / 14-pin |

## 2. Interface and pin-voltage table

| Interface | Signals | Driven by → received by | Voltage domain | Level shifting | Connector / notes |
|---|---|---|---|---|---|
| Host ↔ hub | VBUS, D+, D−, GND | Host ↔ hub | 5 V VBUS, USB differential | n/a | Use a **self-powered** hub; do not rely on host port current for three devices |
| Hub → MCU | VBUS, D±, GND | Hub → MCU USB device | 5 V → 3.3 V on board | n/a | USB-C/Micro-B; CDC-ACM 115200 8N1 framing for telemetry |
| Hub → FPGA | VBUS, D±, GND | Hub → USB-UART bridge | 5 V; board has its own supply | n/a | Board VBUS may be isolated if the barrel supply is used |
| Hub → JTAG | VBUS, D±, GND | Hub → FT2232H | 5 V (adapter logic) | n/a | One USB interface used by OpenOCD; the second may be a UART |
| JTAG → target | TCK, TMS, TDI → target; TDO ← target; nTRST, nSRST (open-drain) | **Adapter drives** TCK/TMS/TDI/nTRST; **target drives** TDO | Target VTREF (3.3 V MCU, VCCO_0 for FPGA) | Built into the adapter (VTREF-referenced buffers). **Never** connect when VTREF = 0 V. | 10-pin 1.27 mm (MCU) or 14-pin 2 mm (FPGA); GND pins all connected |
| VTREF sense | VTREF | Target → adapter (sense only) | 1.8–3.3 V | n/a | The adapter must not back-power the target through VTREF |
| MCU ↔ FPGA data link | UART TX/RX or SPI SCLK/MOSI/MISO/CS | Both directions | 3.3 V (MCU) ↔ bank VCCO (3.3 or 1.8 V) | **Required if VCCO = 1.8 V** (e.g. TXS0108E-class auto-direction or 74LVC1T45 per signal); not needed if the bank is 3.3 V | 0.1" header with 1 GND per 2–3 signals; series 33 Ω near the driver |
| Ground | GND | Common | 0 V | n/a | Single **star point** at the hub/host side; avoid loops through mains-earthed supplies and USB shield |

## 3. JTAG signal ownership

- While a session is attached, the **JTAG adapter is the only driver** of TCK, TMS, TDI and nTRST. The target's own firmware or FPGA fabric must not drive those pins. On the MCU, the SWD/JTAG pins should keep their debug function; on the FPGA, the dedicated configuration-bank JTAG pins are used.
- Only **one adapter** drives a chain at a time. In the software, the JTAG mock reports the capability "owns TCK/TMS/TDI/TDO/nTRST/nSRST while attached", and the design assumes OpenOCD holds the USB interface exclusively.
- Daisy-chaining the MCU and FPGA into one chain is possible only if both TAPs share the same VTREF. With 3.3 V vs 1.8 V they cannot share a chain, so the adapter is moved (or a multiplexed adapter is used) between the 10-pin and 14-pin headers. The block diagram shows "one target at a time".

## 4. Why a JTAG adapter is not a compute board

A JTAG adapter is a **protocol bridge for debug and test access**. It turns USB commands into IEEE 1149.1 TAP state-machine sequences on TCK/TMS/TDI and samples TDO. It can halt cores, read and write memory and registers, run boundary scan, and load FPGA bitstreams, but it executes **no user workload**. It also has no I/O of its own beyond the debug lines, and its electrical interface follows the target's VTREF.

So it cannot be swapped for an MCU or FPGA board in a "device slot". The software models this with a separate `DeviceKind::JtagAdapter` with different capabilities (scan chain, IDCODE read), and its telemetry is a **scan result** (VTREF, TCK rate, chain IDCODEs), not application data.

## 5. Software design (implemented, simulated)

- `IDeviceAdapter` (`src/core/hardware.h`) defines discovery info, capability list, `connect(timeout)`, `disconnect()`, `readTelemetry()` and state (`disconnected`/`connected`/`error`).
- `MockMicrocontroller`, `MockFpga` and `MockJtagAdapter` share `MockAdapterBase`, which provides the state machine, a deterministic noise generator, and **fault injection**: `timeout_once` and `timeout_always`.
- `DeviceManager` is a thread-safe facade with a retry policy (`attempts`, `timeout_ms`) and a timestamped event log.
- **Recoverable failure:** inject `timeout_once` on the FPGA → *Connect* → "Handshake timed out after 300 ms (simulated)" → state `error` → *Retry connect* → `connected`, logged as recovered. With *attempts = 3*, the retry happens inside one call. A telemetry timeout also moves the link to `error` and requires a reconnect.
- Every device card, message and telemetry frame is labelled **SIMULATED**.

## 6. Safe integration plan (for real hardware later)

1. **Inspect and document** each board's schematic: confirm VTREF, VCCO of the JTAG and user banks, 5 V tolerance, and header pinouts. Update the table in §2 accordingly.
2. **Power each board alone** from its intended supply with no inter-board wiring. Measure the rails (3.3/1.8/1.0 V) with a multimeter.
3. **Connect the USB links** through the self-powered hub. Verify enumeration (COM ports, FT2232H) and implement real adapters behind the same `IDeviceAdapter` interface (serial port / libusb / OpenOCD TCL socket).
4. **Attach the JTAG adapter to one target**, with the target powered first and VTREF present. Read the IDCODE only (no programming). Compare it with the datasheet.
5. **Add the MCU↔FPGA link last**, through the level shifter if any bank is 1.8 V. Start with a loopback test at a low baud/SPI clock.
6. **Ground discipline:** a common ground before any signal is connected, and unplug signals before ground.
7. **Software safety:** timeouts on every operation, automatic `error` state on missing responses, no automatic flashing, and an explicit user confirmation before any write or programming operation.

## 7. Limitations

- All behaviour is simulated: no USB enumeration, serial ports, OpenOCD or libusb are used.
- Board classes, voltages and connectors are assumptions from typical dev boards, not verified schematics.
- The mocks model a single telemetry message per read and a simple timeout. Real devices add partial frames, CRC errors, hot-unplug and driver-level failures.
