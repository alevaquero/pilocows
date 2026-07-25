# Phase 1 - Pending Hardware Implementation

These features are designed and UI-ready but require hardware to be attached:

## 1. RFID UART Driver
- **Purpose**: Scan ISO 11784/85 FDX-B tags (11-digit EID)
- **Hardware**: 134.2K AGV FDX-B UART module
- **Pins**: GPIO 10 (RX), GPIO 11 (TX)
- **Status**: Deferred until RFID reader module attached
- **Phase**: Phase 1 completion

## 2. Buzzer
- **Purpose**: Audio feedback for scans/alerts
- **Method**: I2S tone generation via onboard amplifier
- **Pins**: I2S GPIO 35 (LRCK), 36 (BCLK), 37 (DOUT)
- **Status**: Deferred until hardware verification
- **Phase**: Phase 1 completion

## 3. Vibrator Motor
- **Purpose**: Haptic feedback for scans
- **Hardware**: Vibrator motor
- **Pins**: GPIO 21 (PWM control)
- **Status**: Deferred until hardware attached
- **Phase**: Phase 1 completion

All three are toggleable in Settings menu (Task 4), which is implemented.
When hardware is available, wire connections and implement drivers.

Current date: 2026-07-25
