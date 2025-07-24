This uses Windows API (CreateFile, ReadFile, WriteFile) on Windows and POSIX termios on Unix systems. 

Such script configures 8N1 (8 data bits, no parity, 1 stop bit) with hardware flow control disabled, supports standard baud rates from 9600 to 921600, uses overlapped I/O semantics on Windows and O_NONBLOCK on Unix to prevent blocking during read operations, provides real-time bidirectional communication with commands for sending raw hex data (:hex) and text, automatically detects and displays received data in appropriate format (ASCII or hex dump), and proper allocation/deallocation of serial handle structures with error checking.

Does a graceful shutdown on SIGINT/SIGTERM with proper resource cleanup.

---
Compilation;
* Windows: `gcc -o serial_tool serial_tool.c`
* Linux/Unix: `gcc -o serial_tool serial_tool.c`

Examples;
* Windows: `serial_tool.exe COM3 115200`
* Linux: `./serial_tool /dev/ttyUSB0 115200`
