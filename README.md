# ATM Simulator (C Implementation)

A full-featured, terminal-based ATM Simulator written in standard C11 featuring an enclosed asterisk box UI, arrow-key navigation, in-memory **$\mathcal{O}(\log N)$ Binary Search** over card accounts stored in JSON, standalone **PNG thermal receipt** generation, and daily **audit logging**.

---

## Features

1. **Enclosed Terminal UI**:
   - Designed inside a 51-column box with asterisk (`*`) borders.
   - Smooth navigation using keyboard **Arrow Keys (↑ / ↓)** and **[ENTER]**.
   - Masked PIN input (`* * * *`) and automatic digit entry.
   - Colored highlights with ANSI terminal codes.

2. **Card Search via Binary Search**:
   - Data stored in `data/accounts.json`.
   - On startup, cards are sorted by their last 4 digits.
   - Card lookup is executed using **Binary Search** in $\mathcal{O}(\log N)$ time.
   - Rate limiting: card automatically locks after 3 incorrect PIN attempts.

3. **Receipt Generation (`Receipt/` folder)**:
   - Standalone PNG generator with zero external library dependencies (no `libpng` or `cairo` needed).
   - Uses an embedded 8×8 ASCII bitmap font rendered directly onto an RGB pixel buffer.
   - Saves dated receipt images, e.g. `Receipt/receipt_YYYYMMDD_HHMMSS.png`.

4. **Transaction & Audit Logging (`logs/` folder)**:
   - Rotates daily log files: `logs/atm_YYYY-MM-DD.log`.
   - PCI-DSS simulated compliance: card numbers are masked (`****4821`), and PINs are never logged.

---

## Project Structure

```
ATM Simulator/
├── Makefile                # Build system
├── README.md               # Documentation & test accounts
├── data/
│   └── accounts.json       # Seed account database
├── include/
│   ├── account.h           # Account model & binary search interface
│   ├── font8x8.h           # Embedded 8x8 font bitmap table
│   ├── json.h              # Recursive-descent JSON parser/serializer
│   ├── logger.h            # Audit logging module
│   ├── png_writer.h        # Standalone PNG writer (DEFLATE stored blocks)
│   ├── receipt.h           # Receipt renderer
│   └── ui.h                # Terminal raw mode, ANSI menus & box primitives
├── src/
│   ├── account.c           # JSON parser integration & binary search
│   ├── json.c              # Minimal C JSON engine
│   ├── logger.c            # File logger & date rotation
│   ├── main.c              # Application state machine & workflows
│   ├── png_writer.c        # Uncompressed PNG writer with CRC32 & Adler32
│   ├── receipt.c           # RGB canvas drawing & PNG generation
│   └── ui.c                # Raw termios, escape sequence parser & box UI
├── Receipt/                # Output directory for generated PNG receipts
└── logs/                   # Output directory for audit log files
```

---

## How to Build & Run

Open your terminal in this directory:

```bash
cd "/C Programming/ATM Simulator"
```

### 1. Compile
```bash
make
```

### 2. Run the ATM Simulator
```bash
./atm
```
*(Or simply run `make run`)*

---

## Pre-loaded Test Accounts

| Last 4 Digits | 4-Digit PIN | Cardholder Name | Initial Balance | Status |
|:---:|:---:|:---|:---:|:---:|
| `1234` | `4321` | Alice Johnson | $15,420.50 | Active |
| `2468` | `1357` | Bob Williams | $8,750.00 | Active |
| `3690` | `2580` | Carol Davis | $22,100.75 | Active |
| `4821` | `1111` | Anvesh Raman | $3,250.00 | Active |
| `5555` | `9999` | Diana Prince | $50,000.00 | Active |
| `6789` | `7654` | Edward Brown | $1,200.30 | Active |
| `7777` | `0000` | Fiona Green | $45,680.00 | Active |
| `8888` | `3333` | George Miller | $780.25 | Active |

---

## Controls
- **Up Arrow (↑)**: Move selection up
- **Down Arrow (↓)**: Move selection down
- **[ENTER]**: Confirm / Select option
- **[BACKSPACE]**: Delete digit
- **[ESC]**: Cancel / Go back / Shutdown ATM
