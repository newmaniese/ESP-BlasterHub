# ESP32-C3 IR Blaster – Hardware Wiring

This guide explains how to wire the IR receiver and IR transmitter on a breadboard. The firmware uses **GPIO 10** for receive and **GPIO 4** for send.

## Visual Diagram

The diagram below shows the **breadboard view** with **Row 1 at the bottom** and the **USB port facing down**.

![Breadboard wiring – Row 1 at Bottom](assets/wiring-breadboard.svg)

*   **Orientation:** Row 1 at bottom, Row 17 at top.
*   **ESP32:** Sits in Rows 1–8.
*   **Pinout:**
    *   **Left Side (bottom to top):** 5V, GND, 3V3, 4, 3, 2, 1, 0.
    *   **Right Side (bottom to top):** 5, 6, 7, 8, 9, 10, 20, 21.

---

## Parts List

| Part | Role |
|------|------|
| **ESP32-C3** | Board running the firmware. |
| **KY-022** | IR **receiver**. Connect OUT to **GPIO 10**. |
| **IR LED** | IR **emitter**. Driven from **GPIO 4** via a transistor. |
| **2N2222 NPN** | **Transistor**. Switches the LED. |
| **Resistors** | **220 Ω** (for Base); **1 kΩ** (for LED power). |
| **Breadboard** | 17-row (or larger) breadboard. |

---

## Exact Hole-by-Hole Wiring

Use this checklist to verify every connection.

### 1. Board Placement
*   Place the **ESP32-C3** at the bottom of the breadboard (Rows 1–8), straddling the center gutter.
*   **USB port** should be at the bottom (Row 1).
*   **Offset:** Position the board so there are **3 empty holes** to its left and **2 empty holes** to its right.

### 2. ESP32 Connections
*   **GND Wire:** Connect **Left Pin 2** (GND) to **Row 12 Right**.
*   **3V3 Wire:** Connect **Left Pin 3** (3V3) to **Row 13 Right**.
*   **GPIO 4 (Base):** Connect **Left Pin 4** (GPIO 4) to **Row 13 Left** using a **220 Ω resistor**.
*   **GPIO 10 (Signal):** Connect **Right Pin 6** (GPIO 10) to **Row 14 Right** using a **Green wire**.

### 3. Ground Bus
*   **Jumper:** Connect **Row 12 Left** (L5) to **Row 12 Right** (R1) to link the ground on both sides.

### 4. KY-022 Receiver (Right Side)
Place the module on the **Right** side in rows 12–14:
*   **(-) GND:** Leg goes into **Row 12 Right**.
*   **(+) VCC:** Leg goes into **Row 13 Right**.
*   **(S) Signal:** Leg goes into **Row 14 Right**.

### 5. 2N2222 Transistor (Left Side)
Place the transistor on the **Left** side with the **flat edge facing right** (legs in L2):
*   **Emitter (E):** Top leg goes into **Row 12 Left**.
*   **Base (B):** Middle leg goes into **Row 13 Left**.
*   **Collector (C):** Bottom leg goes into **Row 14 Left**.

### 6. IR LED (Left Side)
Place the LED on the **Left** side (far left, L1 column):
*   **Cathode (Short leg/-):** Connects to **Row 14 Left** (joining the Transistor Collector).
*   **Anode (Long leg/+):** Connects to **Row 17 Left**.

### 7. LED Power
*   **Resistor (1 kΩ):** Bridge from **Row 13 Right** (3.3V) to **Row 17 Left** (LED Anode).

---

## Summary of Rows

| Row | Left Side (L) | Right Side (R) |
|-----|---------------|----------------|
| **12** | Transistor Emitter, GND Jumper | **GND Wire**, KY-022 (-), GND Jumper |
| **13** | Transistor Base, **220Ω from Pin 4** | **3V3 Wire**, KY-022 (+), 1kΩ Start |
| **14** | Transistor Collector, LED Cathode (-) | **GPIO 10 Wire**, KY-022 (S) |
| **17** | LED Anode (+), 1kΩ End | *(Empty)* |

Use **3.3 V only**; do not connect 5 V to the ESP32-C3 or the receiver.
