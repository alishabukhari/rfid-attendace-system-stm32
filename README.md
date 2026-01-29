# 📡 RFID Attendance System (STM32)

An embedded RFID-based attendance and logging system built using **STM32 microcontroller**, designed to record RFID card scans with precise timestamps and persistent storage.

This project demonstrates real-world embedded systems engineering using peripherals, non-volatile memory, and structured firmware design.

---

## 🎯 Project Objective

To design a reliable RFID-based system that:
- Reads RFID card IDs
- Logs each entry with timestamp
- Stores data persistently
- Displays logs via external interface
- Allows navigation through saved records

---

## 🛠 Hardware Components

- STM32 Microcontroller (STM32F030 series)
- RFID Reader Module
- RTC (Real-Time Clock)
- EEPROM (Non-volatile storage)
- Push Buttons (Navigation)
- LCD Display
- Power Supply & Wiring

---

## 🧠 System Architecture

RFID Card
↓
RFID Reader
↓
STM32 MCU
├── RTC → Timestamp
├── EEPROM → Log Storage
├── Buttons → Navigation
└── LCD → Display Output

---

## 📁 Project Structure

RFID-Attendance-System/
│
├── firmware/
│ ├── Core/
│ ├── Drivers/
│ ├── Src/
│ ├── Inc/
│ ├── trial.ioc
│ └── STM32F030R8TX_FLASH.ld
│
├── hardware/
│ ├── wiring.jpg
│ ├── rfid-module.jpg
│ └── stm32-board.jpg
│
├── docs/
│ └── system-notes.md
│
├── media/
│ └── demo.mp4
│
└── README.md

---

## ⚙️ Key Features

- RFID card detection and ID extraction
- Timestamping using RTC
- Persistent storage using EEPROM
- Log navigation using hardware buttons
- LCD-based real-time feedback
- Modular firmware design

---

## 🧪 Development Environment

- STM32CubeIDE
- C language
- HAL drivers
- Embedded debugging via ST-Link

---

## 🧠 What I Learned

- Embedded firmware structuring
- Working with RFID communication protocols
- EEPROM data persistence techniques
- Real-time clock integration
- Hardware-software co-design
- Debugging embedded systems

---

## 📸 Hardware Setup

![RFID Setup](hardware/wiring.jpg)

---

## 🚀 Future Improvements

- PC interface for log export
- SD card storage
- Wireless (BLE/WiFi) logging
- Improved LCD UI

---

👩‍💻 **Author:** Alisha Bukhari  
📍 Computer Engineering | Embedded Systems
