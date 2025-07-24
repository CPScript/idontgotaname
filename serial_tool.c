#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
    typedef HANDLE serial_port_t;
    #define INVALID_PORT INVALID_HANDLE_VALUE
#else
    #include <termios.h>
    #include <sys/select.h>
    typedef int serial_port_t;
    #define INVALID_PORT -1
#endif

typedef struct {
    serial_port_t port;
    char port_name[256];
    int baud_rate;
    int is_open;
} serial_handle_t;

static volatile int running = 1;

void signal_handler(int sig) {
    running = 0;
    printf("\nShutting down...\n");
}

#ifdef _WIN32
int configure_serial_windows(serial_handle_t *handle) {
    DCB dcb = {0};
    COMMTIMEOUTS timeouts = {0};
    
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle->port, &dcb)) {
        fprintf(stderr, "Error getting comm state: %d\n", GetLastError());
        return -1;
    }
    
    dcb.BaudRate = handle->baud_rate;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fAbortOnError = FALSE;
    
    if (!SetCommState(handle->port, &dcb)) {
        fprintf(stderr, "Error setting comm state: %d\n", GetLastError());
        return -1;
    }
    
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    
    if (!SetCommTimeouts(handle->port, &timeouts)) {
        fprintf(stderr, "Error setting timeouts: %d\n", GetLastError());
        return -1;
    }
    
    return 0;
}

serial_handle_t* open_serial_port(const char *port_name, int baud_rate) {
    serial_handle_t *handle = calloc(1, sizeof(serial_handle_t));
    if (!handle) return NULL;
    
    strncpy(handle->port_name, port_name, sizeof(handle->port_name) - 1);
    handle->baud_rate = baud_rate;
    
    handle->port = CreateFileA(port_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (handle->port == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error opening port %s: %d\n", port_name, GetLastError());
        free(handle);
        return NULL;
    }
    
    if (configure_serial_windows(handle) != 0) {
        CloseHandle(handle->port);
        free(handle);
        return NULL;
    }
    
    handle->is_open = 1;
    return handle;
}

int serial_write(serial_handle_t *handle, const void *data, size_t len) {
    DWORD bytes_written;
    if (!WriteFile(handle->port, data, len, &bytes_written, NULL)) {
        fprintf(stderr, "Write error: %d\n", GetLastError());
        return -1;
    }
    return bytes_written;
}

int serial_read(serial_handle_t *handle, void *buffer, size_t len) {
    DWORD bytes_read;
    if (!ReadFile(handle->port, buffer, len, &bytes_read, NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_TIMEOUT) {
            fprintf(stderr, "Read error: %d\n", error);
            return -1;
        }
        return 0;
    }
    return bytes_read;
}
#else
speed_t get_baud_rate(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return B9600;
    }
}

serial_handle_t* open_serial_port(const char *port_name, int baud_rate) {
    serial_handle_t *handle = calloc(1, sizeof(serial_handle_t));
    if (!handle) return NULL;
    
    strncpy(handle->port_name, port_name, sizeof(handle->port_name) - 1);
    handle->baud_rate = baud_rate;
    
    handle->port = open(port_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (handle->port == INVALID_PORT) {
        fprintf(stderr, "Error opening port %s: %s\n", port_name, strerror(errno));
        free(handle);
        return NULL;
    }
    
    struct termios tty;
    if (tcgetattr(handle->port, &tty) != 0) {
        fprintf(stderr, "Error getting attributes: %s\n", strerror(errno));
        close(handle->port);
        free(handle);
        return NULL;
    }
    
    speed_t speed = get_baud_rate(baud_rate);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;
    
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;
    
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;
    
    tty.c_cc[VTIME] = 1;
    tty.c_cc[VMIN] = 0;
    
    if (tcsetattr(handle->port, TCSANOW, &tty) != 0) {
        fprintf(stderr, "Error setting attributes: %s\n", strerror(errno));
        close(handle->port);
        free(handle);
        return NULL;
    }
    
    handle->is_open = 1;
    return handle;
}

int serial_write(serial_handle_t *handle, const void *data, size_t len) {
    ssize_t bytes_written = write(handle->port, data, len);
    if (bytes_written < 0) {
        fprintf(stderr, "Write error: %s\n", strerror(errno));
        return -1;
    }
    return bytes_written;
}

int serial_read(serial_handle_t *handle, void *buffer, size_t len) {
    ssize_t bytes_read = read(handle->port, buffer, len);
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        fprintf(stderr, "Read error: %s\n", strerror(errno));
        return -1;
    }
    return bytes_read;
}
#endif

void close_serial_port(serial_handle_t *handle) {
    if (!handle) return;
    
    if (handle->is_open) {
#ifdef _WIN32
        CloseHandle(handle->port);
#else
        close(handle->port);
#endif
        handle->is_open = 0;
    }
    free(handle);
}

void print_hex(const unsigned char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (len % 16 != 0) printf("\n");
}

void interactive_mode(serial_handle_t *handle) {
    char input_buffer[1024];
    unsigned char read_buffer[1024];
    int bytes_read;
    
    printf("Serial Communication Tool - Interactive Mode\n");
    printf("Port: %s @ %d baud\n", handle->port_name, handle->baud_rate);
    printf("Commands:\n");
    printf("  :hex <data>  - Send hex data (e.g., :hex 48656C6C6F)\n");
    printf("  :quit        - Exit program\n");
    printf("  <text>       - Send text data\n\n");
    
    while (running) {
        printf("> ");
        fflush(stdout);
        
        // Check for incoming data
        bytes_read = serial_read(handle, read_buffer, sizeof(read_buffer) - 1);
        if (bytes_read > 0) {
            printf("\nReceived (%d bytes): ", bytes_read);
            read_buffer[bytes_read] = '\0';
            
            // Check if data is printable
            int is_printable = 1;
            for (int i = 0; i < bytes_read; i++) {
                if (read_buffer[i] < 32 && read_buffer[i] != '\n' && read_buffer[i] != '\r' && read_buffer[i] != '\t') {
                    is_printable = 0;
                    break;
                }
            }
            
            if (is_printable) {
                printf("'%s'\n", read_buffer);
            } else {
                printf("\n");
                print_hex(read_buffer, bytes_read);
            }
            printf("> ");
            fflush(stdout);
        }
        
        // Check for user input
#ifdef _WIN32
        if (_kbhit()) {
            if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) break;
#else
        struct timeval tv = {0, 100000}; // 100ms timeout
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        
        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0) {
            if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) break;
#endif
            
            // Remove newline
            size_t len = strlen(input_buffer);
            if (len > 0 && input_buffer[len-1] == '\n') {
                input_buffer[len-1] = '\0';
                len--;
            }
            
            if (len == 0) continue;
            
            if (strcmp(input_buffer, ":quit") == 0) {
                break;
            }
            
            if (strncmp(input_buffer, ":hex ", 5) == 0) {
                // Parse hex data
                char *hex_str = input_buffer + 5;
                unsigned char hex_data[512];
                size_t hex_len = 0;
                
                for (size_t i = 0; i < strlen(hex_str) && hex_len < sizeof(hex_data); i += 2) {
                    if (hex_str[i] == ' ') {
                        i--;
                        continue;
                    }
                    
                    unsigned int byte;
                    if (sscanf(&hex_str[i], "%2x", &byte) == 1) {
                        hex_data[hex_len++] = (unsigned char)byte;
                    }
                }
                
                if (hex_len > 0) {
                    int sent = serial_write(handle, hex_data, hex_len);
                    if (sent > 0) {
                        printf("Sent %d bytes (hex)\n", sent);
                    }
                }
            } else {
                // Send text data
                int sent = serial_write(handle, input_buffer, len);
                if (sent > 0) {
                    printf("Sent %d bytes: '%s'\n", sent, input_buffer);
                }
            }
        }
        
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }
}

void print_usage(const char *program_name) {
    printf("Usage: %s <port> [baud_rate]\n", program_name);
    printf("Examples:\n");
#ifdef _WIN32
    printf("  %s COM3 115200\n", program_name);
    printf("  %s COM1\n", program_name);
#else
    printf("  %s /dev/ttyUSB0 115200\n", program_name);
    printf("  %s /dev/ttyACM0\n", program_name);
#endif
    printf("\nDefault baud rate: 9600\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *port_name = argv[1];
    int baud_rate = (argc > 2) ? atoi(argv[2]) : 9600;
    
    signal(SIGINT, signal_handler);
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
#endif
    
    serial_handle_t *handle = open_serial_port(port_name, baud_rate);
    if (!handle) {
        fprintf(stderr, "Failed to open serial port %s\n", port_name);
        return 1;
    }
    
    printf("Successfully opened %s at %d baud\n", port_name, baud_rate);
    
    interactive_mode(handle);
    
    close_serial_port(handle);
    printf("Serial port closed.\n");
    
    return 0;
}
