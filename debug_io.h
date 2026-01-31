/*
  debug_io.h - Debug logging system for MiST firmware
  
  Provides comprehensive logging for TAP/IDX file operations
  and SPI data transmission to FPGA.
  
  Reads DEBUG_IO_ENABLED from mist.ini
  
  Usage:
    1. Call debug_io_init() at program start
    2. Set debug_io_enable_from_ini() with value from mist.ini
    3. Enable logging with debug_io_enable() for specific file types
    4. Use logging functions throughout your code
    5. Call debug_io_close() when done
*/

#ifndef DEBUG_IO_H
#define DEBUG_IO_H

#include "fat_compat.h"

// ========================================================================
// DEBUG CONFIGURATION
// ========================================================================

// Debug is now controlled at runtime via mist.ini
// No longer a compile-time constant
// #define DEBUG_IO_ENABLED 1

// Debug log file names
#define DEBUG_IO_LOGFILE      "data_io_debug.log"
#define DEBUG_IDX_LOGFILE     "idx_debug.log"

// Maximum buffer sizes for formatting
#define DEBUG_BUFFER_SIZE     256
#define DEBUG_HEX_BUFFER_SIZE 512

// ========================================================================
// DEBUG CONTROL FUNCTIONS
// ========================================================================

/**
 * Initialize the debug system
 * Must be called before any other debug functions
 */
void debug_io_init(void);

/**
 * Enable or disable the entire debug system based on mist.ini
 * This should be called after parsing the INI file
 * @param enabled 1 to enable debug system, 0 to disable
 */
void debug_io_set_enabled_from_ini(int enabled);

/**
 * Check if debug system is globally enabled
 * @return 1 if enabled via INI, 0 if disabled
 */
int debug_io_get_global_enabled(void);

/**
 * Enable logging for data_io operations
 * Only works if debug system is globally enabled via INI
 * @param ext File extension to enable logging for (e.g., "TAP", "PRG")
 *            Pass NULL to enable for all files
 */
void debug_io_enable(const char *ext);

/**
 * Check if data_io logging is currently enabled
 * @return 1 if enabled, 0 if disabled
 */
int debug_io_is_enabled(void);

/**
 * Enable logging for idx_files operations
 * Only works if debug system is globally enabled via INI
 */
void debug_idx_enable(void);

/**
 * Check if idx logging is currently enabled
 * @return 1 if enabled, 0 if disabled
 */
int debug_idx_is_enabled(void);

/**
 * Close debug log files and cleanup
 */
void debug_io_close(void);

/**
 * Close IDX debug log file
 */
void debug_idx_close(void);

// ========================================================================
// GENERAL LOGGING FUNCTIONS
// ========================================================================

/**
 * Log a formatted message to data_io debug log
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
void debug_io_log(const char *format, ...);

/**
 * Log a formatted message to idx debug log
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
void debug_idx_log(const char *format, ...);

/**
 * Log binary data as hexadecimal to data_io log
 * @param label Description of the data
 * @param data Pointer to data buffer
 * @param len Number of bytes to log
 */
void debug_io_log_hex(const char *label, unsigned char *data, int len);

/**
 * Log binary data as hexadecimal to idx log
 * @param label Description of the data
 * @param data Pointer to data buffer
 * @param len Number of bytes to log
 */
void debug_idx_log_hex(const char *label, unsigned char *data, int len);

// ========================================================================
// SPI TRANSACTION LOGGING
// ========================================================================

/**
 * Log an SPI command with description
 * @param cmd Command byte
 * @param description Human-readable description
 */
void debug_io_log_spi_cmd(unsigned char cmd, const char *description);

/**
 * Log SPI data write operation with position tracking
 * @param data Pointer to data buffer
 * @param len Number of bytes written
 * @param context Description of the write context
 */
void debug_io_log_spi_write(unsigned char *data, int len, const char *context);

/**
 * Reset the SPI byte counter to zero
 */
void debug_io_reset_spi_counter(void);

/**
 * Get current SPI byte counter value
 * @return Total number of bytes sent via SPI
 */
unsigned int debug_io_get_spi_counter(void);

// ========================================================================
// TAP FILE ANALYSIS
// ========================================================================

/**
 * Analyze and log TAP file header information
 * @param header Pointer to 20-byte TAP header
 */
void debug_io_analyze_tap_header(unsigned char *header);

/**
 * Analyze and log TAP data bytes (pilot tones, etc.)
 * @param data Pointer to TAP data
 * @param len Number of bytes to analyze
 * @param offset Starting offset in the TAP file
 */
void debug_io_analyze_tap_data(unsigned char *data, int len, unsigned int offset);

// ========================================================================
// SECTION MARKERS
// ========================================================================

/**
 * Log a major section separator
 * @param title Section title
 */
void debug_io_section(const char *title);

/**
 * Log a subsection separator
 * @param title Subsection title
 */
void debug_io_subsection(const char *title);

/**
 * Log a major section separator to IDX log
 * @param title Section title
 */
void debug_idx_section(const char *title);

/**
 * Log a subsection separator to IDX log
 * @param title Subsection title
 */
void debug_idx_subsection(const char *title);

// ========================================================================
// HELPER MACROS
// ========================================================================

// Convenience macros for common operations
#define DEBUG_IO_START(name) \
    debug_io_section("START: " name)

#define DEBUG_IO_END(name) \
    debug_io_section("END: " name)

#define DEBUG_IDX_START(name) \
    debug_idx_section("START: " name)

#define DEBUG_IDX_END(name) \
    debug_idx_section("END: " name)

#endif // DEBUG_IO_H
