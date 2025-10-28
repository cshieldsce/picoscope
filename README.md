# Picoscope - Pico 2 W Wi-Fi Oscilloscope (A FreeRTOS + DMA Lab)

A simple, Wi-Fi-enabled digital oscilloscope using a Raspberry Pi Pico 2 W, FreeRTOS, and a web browser.

## Project Phases

This project is built in a series of phases. The `main` branch will always contain the most recent stable phase.

### Phase 1: Hello, FreeRTOS! (Complete)
* **Goal:** Prove the FreeRTOS scheduler is running on the Pico 2 W.
* **Outcome:** The onboard LED blinks from a simple, low-priority FreeRTOS task.

### Phase 2: The Web Server (Complete)
* **Goal:** Get the Pico on the Wi-Fi and serve a basic HTTP page.
* **Outcome:** A new `vWebServerTask` (low priority) will connect to Wi-Fi and serve a "Hello World" page to a browser, while the `vBlinkTask` continues to run, proving multitasking.

### Phase 3: The Acquisition Engine
* **Goal:** Capture ADC data at a high, fixed sample rate (e.g., 250kSPS) using zero CPU time.
* **Outcome:** A new `vAcquisitionTask` (high priority) will configure a DMA channel to continuously read from an ADC pin and fill a buffer in the background.

### Phase 4: The Data Pipeline
* **Goal:** Safely pass the full data buffer from the high-priority ADC task to the low-priority web task.
* **Outcome:** Use a FreeRTOS **Binary Semaphore** to signal when data is ready. 

### Phase 5: Real-Time Streaming
* **Goal:** Upgrade the HTTP server to a WebSocket server for continuous, real-time data streaming.
* **Outcome:** The `vWebServerTask` will perform a WebSocket handshake. When a client is connected, it will wait for the semaphore from Phase 4 and immediately send the new data as a WebSocket binary frame.

### Phase 6: The Frontend
* **Goal:** Visualize the data in the browser.
* **Outcome:** An `index.html` file (served by the Pico) will use JavaScript to connect to the WebSocket, receive the binary data, convert it to voltage, and plot it live on an HTML `<canvas>` using a charting library.

## Hardware Requirements

- Raspberry Pi Pico 2 W (RP2350)

## Software Requirements

- Pico SDK v2.1.0 or later
- FreeRTOS-Kernel
- CMake 3.13+
- ARM GCC toolchain

## Configuration

### Critical lwIP Configuration

This project requires specific lwIP settings to work properly with FreeRTOS and BSD sockets on RP2350. The key configuration is in `lwipopts.h`:

**Must-have settings:**
```c
#define DEFAULT_ACCEPTMBOX_SIZE 32      // Critical for listen() to work
#define MEMP_NUM_NETCONN 16             // Socket connections
#define MEMP_NUM_TCP_PCB 16             // TCP control blocks
#define MEMP_NUM_TCP_PCB_LISTEN 16      // Listening sockets
#define TCPIP_MBOX_SIZE 32              // TCPIP thread mailbox
```

Without these, you'll get a `*** PANIC *** size > 0` error during socket operations.

### FreeRTOS Heap Size

In `FreeRTOSConfig.h`:
```c
#define configTOTAL_HEAP_SIZE (256*1024)  // 256KB heap required (for now)
```

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Flashing

Hold the BOOTSEL button while plugging in the Pico 2 W, then:

```bash
cp picoscope.uf2 /path/to/RPI-RP2
```

Or use picotool:
```bash
picotool load picoscope.uf2
picotool reboot
```

## Usage

1. Flash the firmware
2. Connect via USB serial (115200 baud) to see debug output
3. The Pico will connect to your WiFi network
4. Note the IP address printed in serial output (e.g., `192.198.1.164`)
5. Open a browser and navigate to `http://[IP_ADDRESS]`
6. You should see "Hello" displayed on the webpage
7. The onboard LED will blink at 2Hz

## CMakeLists.txt Configuration

Key library linking:
```cmake
target_link_libraries(picoscope 
    pico_stdlib
    pico_cyw43_arch_lwip_sys_freertos  # NOT threadsafe_background
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)
```

Use `pico_cyw43_arch_lwip_sys_freertos` for FreeRTOS integration, not the polling variants.

## Notes

- This configuration is specific to RP2350 (Pico 2 W) with SDK v2.1.0
- The `DEFAULT_ACCEPTMBOX_SIZE` parameter was critical to fix socket panics