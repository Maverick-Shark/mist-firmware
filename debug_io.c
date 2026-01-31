/*
  debug_io.c - Debug logging system implementation
  
  Comprehensive logging for TAP/IDX file operations and SPI transfers
*/

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "debug_io.h"
#include "debug.h"  // For iprintf
#include "fat_compat.h"

// ========================================================================
// INTERNAL STATE
// ========================================================================

// File handles
static FIL data_io_logfile;
static FIL idx_logfile;

// Global debug control (from mist.ini)
static int debug_io_globally_enabled = 0;

// State flags
static int data_io_log_initialized = 0;
static int data_io_log_enabled = 0;
static int idx_log_initialized = 0;
static int idx_log_enabled = 0;

// SPI transaction tracking
static unsigned int spi_byte_counter = 0;

// ========================================================================
// INITIALIZATION AND CONTROL
// ========================================================================

void debug_io_init(void) {
    // Reset all state
    data_io_log_initialized = 0;
    data_io_log_enabled = 0;
    idx_log_initialized = 0;
    idx_log_enabled = 0;
    spi_byte_counter = 0;
}

void debug_io_set_enabled_from_ini(int enabled) {
    debug_io_globally_enabled = enabled;
    iprintf("DEBUG_IO: Global debug %s (from mist.ini)\n", 
            enabled ? "ENABLED" : "DISABLED");
}

int debug_io_get_global_enabled(void) {
    return debug_io_globally_enabled;
}

void debug_io_enable(const char *ext) {
    // Only enable if globally enabled
    if (!debug_io_globally_enabled) {
        return;
    }
    
    // Enable for TAP files, or all files if ext is NULL
    if (!ext || (strcmp(ext, "TAP") == 0 || strcmp(ext, "tap") == 0)) {
        data_io_log_enabled = 1;
        
        // Open log file if not already open
        if (!data_io_log_initialized) {
            FRESULT res = f_open(&data_io_logfile, DEBUG_IO_LOGFILE, 
                                FA_WRITE | FA_CREATE_ALWAYS);
            if (res == FR_OK) {
                data_io_log_initialized = 1;
                spi_byte_counter = 0;
                iprintf("DEBUG_IO: Log file '%s' created\n", DEBUG_IO_LOGFILE);
            } else {
                iprintf("DEBUG_IO: Failed to create log file (error %d)\n", res);
            }
        }
    }
}

int debug_io_is_enabled(void) {
    return debug_io_globally_enabled && data_io_log_enabled && data_io_log_initialized;
}

void debug_idx_enable(void) {
    // Only enable if globally enabled
    if (!debug_io_globally_enabled) {
        return;
    }
    
    idx_log_enabled = 1;
    
    // Open log file if not already open
    if (!idx_log_initialized) {
        FRESULT res = f_open(&idx_logfile, DEBUG_IDX_LOGFILE, 
                            FA_WRITE | FA_CREATE_ALWAYS);
        if (res == FR_OK) {
            idx_log_initialized = 1;
            //iprintf("DEBUG_IDX: Log file '%s' created\n", DEBUG_IDX_LOGFILE);
            debug_io_log("DEBUG_IDX: Log file '%s' created\n", DEBUG_IDX_LOGFILE);
        } else {
            //iprintf("DEBUG_IDX: Failed to create log file (error %d)\n", res);
            debug_io_log("DEBUG_IDX: Failed to create log file (error %d)\n", res);
        }
    }
}

int debug_idx_is_enabled(void) {
    return debug_io_globally_enabled && idx_log_enabled && idx_log_initialized;
}

void debug_io_close(void) {
    if (data_io_log_initialized) {
        // Log final summary
        if (spi_byte_counter > 0) {
            //iprintf("DEBUG_IO: Total SPI bytes sent: 0x%08X (%u)\n", 
            //       spi_byte_counter, spi_byte_counter);
            debug_io_log("DEBUG_IO: Total SPI bytes sent: 0x%08X (%u)\n", 
                   spi_byte_counter, spi_byte_counter);
        }
        
        f_close(&data_io_logfile);
        data_io_log_initialized = 0;
        data_io_log_enabled = 0;
        spi_byte_counter = 0;
        //iprintf("DEBUG_IO: Log file closed\n");
        debug_io_log("DEBUG_IO: Log file closed\n");
    }
}

void debug_idx_close(void) {
    if (idx_log_initialized) {
        f_close(&idx_logfile);
        idx_log_initialized = 0;
        idx_log_enabled = 0;
        //iprintf("DEBUG_IDX: Log file closed\n");
        debug_io_log("DEBUG_IDX: Log file closed\n");
    }
}

// ========================================================================
// GENERAL LOGGING FUNCTIONS
// ========================================================================

void debug_io_log(const char *format, ...) {
    if (!debug_io_is_enabled()) return;
    
    char buffer[DEBUG_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsiprintf(buffer, format, args);
    va_end(args);

    UINT b_w;
    f_write(&data_io_logfile, buffer, strlen(buffer), &b_w);
    f_sync(&data_io_logfile);

    // Also print to console
    iprintf("%s", buffer);
}

void debug_idx_log(const char *format, ...) {
    if (!debug_idx_is_enabled()) return;
    
    char buffer[DEBUG_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsiprintf(buffer, format, args);
    va_end(args);

    UINT b_w;
    f_write(&idx_logfile, buffer, strlen(buffer), &b_w);
    f_sync(&idx_logfile);

    // Also print to console
    iprintf("%s", buffer);
}

void debug_io_log_hex(const char *label, unsigned char *data, int len) {
    if (!debug_io_is_enabled()) return;

    //iprintf("%s (%d bytes): ", label, len);
    debug_io_log("%s (%d bytes): ", label, len);

    // Limit display to 32 bytes
    int bytes_to_show = (len > 32) ? 32 : len;
    
    for (int i = 0; i < bytes_to_show; i++) {
        if (i > 0 && (i % 16) == 0) {
            //iprintf("\n");
            debug_io_log("\n");
        }
        //iprintf("%02X ", data[i]);
        debug_io_log("%02X ", data[i]);
    }
    
    if (len > 32) {
        //iprintf("... [+%d more]", len - 32);
        debug_io_log("... [+%d more]", len - 32);
    }
    //iprintf("\n");
    debug_io_log("\n");
}

void debug_idx_log_hex(const char *label, unsigned char *data, int len) {
    if (!debug_idx_is_enabled()) return;

    debug_idx_log("%s (%d bytes): ", label, len);

    // Limit display to 32 bytes
    int bytes_to_show = (len > 32) ? 32 : len;
    
    for (int i = 0; i < bytes_to_show; i++) {
        if (i > 0 && (i % 16) == 0) {
            //iprintf("\n");
            debug_idx_log("\n");
        }
        //iprintf("%02X ", data[i]);
        debug_idx_log("%02X ", data[i]);
    }
    
    if (len > 32) {
        //iprintf("... [+%d more]", len - 32);
        debug_idx_log("... [+%d more]", len - 32);
    }
    //iprintf("\n");
    debug_idx_log("\n");
}

// ========================================================================
// SPI TRANSACTION LOGGING
// ========================================================================

void debug_io_log_spi_cmd(unsigned char cmd, const char *description) {
    if (!debug_io_is_enabled()) return;

    //iprintf(">>> SPI CMD: 0x%02X (%s)\n", cmd, description);
    debug_io_log(">>> SPI CMD: 0x%02X (%s)\n", cmd, description);
}

void debug_io_log_spi_write(unsigned char *data, int len, const char *context) {
    if (!debug_io_is_enabled()) return;

    //iprintf(">>> SPI WRITE [%s]: %d bytes (total: 0x%08X)\n", 
    //      context, len, spi_byte_counter);
    debug_io_log(">>> SPI WRITE [%s]: %d bytes (global pos: 0x%08X)\n", 
            context, len, spi_byte_counter);

    spi_byte_counter += len;
}

void debug_io_reset_spi_counter(void) {
    spi_byte_counter = 0;
}

unsigned int debug_io_get_spi_counter(void) {
    return spi_byte_counter;
}

// ========================================================================
// TAP FILE ANALYSIS
// ========================================================================

void debug_io_analyze_tap_header(unsigned char *header) {
    if (!debug_io_is_enabled()) return;

    //iprintf("\nTAP Header Analysis:\n");
    debug_io_log("\nTAP Header Analysis:\n");
    debug_io_log_hex("  Header bytes", header, 20);

    if (memcmp(header, "C64-TAPE-RAW", 12) == 0) {

        unsigned int tap_data_size = 
            (header[0x13] << 24) |
            (header[0x12] << 16) |
            (header[0x11] <<  8) |
            (header[0x10] <<  0);
        
        //iprintf("  Valid TAP signature\n");
        //iprintf("  Version: 0x%02X\n", header[0x0C]);
        //iprintf("  Data size: %u bytes\n", tap_data_size);

        debug_io_log("  Data size field: 0x%08X (%u bytes)\n", 
                    tap_data_size, tap_data_size);
        debug_io_log("  Expected total file size: 0x%08X (header 20 + data %u)\n",
                    tap_data_size + 20, tap_data_size);

    } else {
	iprintf("  WARNING: Invalid TAP signature\n");
    }
}

void debug_io_analyze_tap_data(unsigned char *data, int len, unsigned int offset) {
    if (!debug_io_is_enabled()) return;

    if (len > 0) {
        //iprintf("  Chunk at offset 0x%04X, first byte: 0x%02X", offset, data[0]);
        //debug_io_log("\nTAP Data Analysis (offset 0x%04X):\n", offset);
        debug_io_log("-Chunk at offset 0x%04X, first byte: 0x%02X", offset, data[0]);

        unsigned char b = data[0];

        // Classify the byte
        if (b == 0x00) {
            //iprintf(" (OVERFLOW)");
            debug_io_log(" OVERFLOW (24-bit follows)\n");
        } else if (b >= 0x28 && b <= 0x3C) {
            iprintf(" (PILOT)");
            debug_io_log(" (PILOT)\n");
        }
    }
    iprintf("\n");
}

// ========================================================================
// SECTION MARKERS
// ========================================================================

void debug_io_section(const char *title) {
    if (!debug_io_is_enabled()) return;

    //iprintf("\n========================================\n");
    //iprintf(" %s\n", title);
    //iprintf("========================================\n");

    debug_io_log("\n");
    debug_io_log("========================================\n");
    debug_io_log(" %s\n", title);
    debug_io_log("========================================\n");
}

void debug_io_subsection(const char *title) {
    if (!debug_io_is_enabled()) return;

    //iprintf("\n--- %s ---\n", title);
    debug_idx_log("\n--- %s ---\n", title);
}

void debug_idx_section(const char *title) {
    if (!debug_idx_is_enabled()) return;

    //iprintf("\n========================================\n");
    //iprintf(" %s\n", title);
    //iprintf("========================================\n");

    debug_io_log("\n");
    debug_io_log("========================================\n");
    debug_io_log(" %s\n", title);
    debug_io_log("========================================\n");
}

void debug_idx_subsection(const char *title) {
    if (!debug_idx_is_enabled()) return;

    //iprintf("\n--- %s ---\n", title);
    debug_idx_log("\n--- %s ---\n", title);

}
