# MemStream

A zero-copy, hardware-to-network streaming engine for the Raspberry Pi Pico 2 W (RP2350). 

MemStream acts as a wireless oscilloscope. It captures analog signals via hardware DMA (zero CPU overhead) and blasts them over WiFi via WebSockets using a FreeRTOS-managed TCP/IP stack. 

## Architecture

![Architecture Diagram](docs/architecture_diagram.png)

The pipeline is entirely zero-copy, eliminating expensive memory-to-memory transfers between acquisition and transmission:

1. **The Producer (Hardware):** The ADC is tied directly to the DMA engine, streaming into memory buffers at up to 500 kS/s. This guarantees hard real-time, deterministic sampling regardless of network load.
2. **The Consumer (Network):** A dual-core FreeRTOS SMP setup manages task synchronization. Upon buffer completion, Task Notifications instantly wake the Mongoose web server, which publishes the raw memory buffers directly to connected WebSocket clients.

## Build & Flash

*Dependencies: CMake, Arm-GCC Toolchain, Pico-SDK*

```bash
mkdir build && cd build
cmake ..
make
# Drop the compiled .uf2 onto your Raspberry Pi Pico 2 W.
```

## Demo

Video demo of the oscilloscope successfully streaming a square wave signal over WiFi via the Pico 2 W.

<img src="docs/scope.gif"  width="795" height="703">

## References

Some references I used for this project:

- [AWG with Raspberry Pi Pico on Autodesk Instructables](https://www.instructables.com/Arbitrary-Wave-Generator-With-the-Raspberry-Pi-Pic/)
- ['Dr Jon EA: Pico & Pico 2' on YouTube for his series on FreeRTOS](https://www.youtube.com/@DrJonEA)
