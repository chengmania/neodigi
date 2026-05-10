# neodigi

**Modern Qt6 UI for the fldigi digital modem engine.**

neodigi replaces fldigi's built-in FLTK interface with a clean, modern Qt6 UI while keeping all DSP, modem, soundcard, and CAT control running on the stock fldigi backend under the hood. Communication is exclusively via XML-RPC — no patches to fldigi required.

![screenshot](docs/screenshot.png)

## Features

- **Waterfall** — real-time FFT spectrum display with PortAudio capture + FFTW
- **RX/TX text panes** — green-on-black receive, amber transmit buffer
- **Macro buttons** — customizable grid with inline editor
- **Mode selector** — click the mode pill to browse and switch modems
- **Frequency control** — click-to-edit VFO display with step buttons
- **Carrier tuning** — fine-tune modem audio carrier with step buttons
- **Logbook** — QSO logging with ADIF export, inline notes, per-entry edit/delete
- **Callsign lookup** — automatic via QRZ.com or HamDB on double-click or tab-out
- **Dark / Light themes** — toggle in the View menu
- **Resizable sidebar** — QSplitter with state persistence
- **Waterfall audio source** — select or auto-detect input device
- **fldigi path config** — point to your fldigi install; saved across launches

## Dependencies

| Package | Purpose |
|---------|---------|
| Qt 6 (Core, Widgets, Network) | UI framework |
| PortAudio v19 | Audio capture for waterfall |
| FFTW3 | FFT computation |
| libpulse-simple | PulseAudio/PipeWire integration |
| fldigi | Modem engine (backend) |
| cmake | Build system |
| nc, curl | Used by launch script for readiness checks |

### Ubuntu / Debian

```bash
sudo apt install qt6-base-dev libportaudio2 libportaudio-dev \
                 libfftw3-dev libpulse-dev cmake g++ fldigi netcat-openbsd curl
```

### Fedora

```bash
sudo dnf install qt6-qtbase-devel portaudio-devel fftw-devel \
                 pulseaudio-libs-devel cmake gcc-c++ fldigi nmap-ncat curl
```

### Arch Linux

```bash
sudo pacman -S qt6-base portaudio fftw cmake gcc fldigi netcat curl
```

## Build

```bash
cd app
cmake -B build
cmake --build build -j$(nproc)
```

## Run

**First run** — the launch script detects no fldigi config, starts the fldigi setup wizard, then exits. Complete the wizard and run again:

```bash
./neodigi-launch.sh
```

On the first run, if fldigi is not found in your PATH, the script will prompt you to locate the fldigi binary. The path is saved for subsequent launches.

**Subsequent runs** — launches fldigi minimized, waits for XML-RPC, then opens the neodigi Qt UI. Closing the UI also stops fldigi.

**Development / standalone mode** — run neodigi without fldigi to test the UI:

```bash
cd app
./build/neodigi
```

Then go to **Settings → XML-RPC Connection…** and enable **Stub Mode** (or start fldigi separately and connect).

## How it works

```
┌─────────────────┐     XML-RPC      ┌─────────────────┐
│  fldigi (min.)  │ ◄──────────────► │  neodigi (Qt6)  │
│  - DSP          │    port 7362     │  - Waterfall     │
│  - Modems       │                  │  - RX/TX text    │
│  - Soundcard    │                  │  - Macros        │
│  - CAT control  │                  │  - Logbook       │
└─────────────────┘                  └─────────────────┘
```

fldigi runs minimized in the background owning all DSP, modem, and hardware I/O. neodigi provides the user interface only. Neither breaks if the other is updated independently. Companion apps (flrig, flmsg, flamp) connect to fldigi directly as normal.

## Architecture

```
neodigi/
├── neodigi-launch.sh      # Startup script
├── app/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── MainWindow.h/.cpp       # Main UI wiring
│   ├── WaterfallWidget.h/.cpp  # FFT waterfall display
│   ├── FLDigiClient.h/.cpp     # XML-RPC interface
│   ├── Sidebar.h/.cpp          # Control panel
│   ├── StatusBar.h/.cpp        # Bottom status bar
│   ├── MacroGrid.h/.cpp        # Macro buttons
│   ├── FreqDisplay.h/.cpp      # 7-segment frequency
│   ├── Logbook.h/.cpp          # QSO logging + ADIF export
│   └── resources/              # QSS themes, icons
└── docs/
    └── neodigi_mockup_v2-5.html
```

## Author

**Greg Cheng (KC3SMW)** — [GitHub](https://github.com/chengmania)

## License

**CC BY-NC-SA 4.0** — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.

You are free to use, share, and modify this software as long as:
- **Attribution** — credit the author (Greg Cheng / KC3SMW)
- **NonCommercial** — no commercial use or monetization
- **ShareAlike** — derivative works must use the same license

See [LICENSE](LICENSE) for the full text.

*Note: The fldigi modem engine is licensed under GPL v3 and is not covered by this license. neodigi communicates with a stock, unmodified fldigi via XML-RPC and is a separate program.*
