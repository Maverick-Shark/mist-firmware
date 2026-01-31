/*
  idx_files.c - IDX file handler for MiST firmware
  
  Modified version with modular debug system
*/

#include <stdlib.h>
#include <string.h>

#include "idx_files.h"
#include "fat_compat.h"
#include "data_io.h"
#include "menu.h"
#include "osd.h"
#include "debug_io.h"  // << DEBUG SYSTEM

#define IDX_EOT                 4
#define CHAR_IS_LINEEND(c)      (((c) == '\n') || ((c) == '\r'))
#define CHAR_IS_COMMENT(c)      (((c) == ';'))
#define CHAR_IS_QUOTE(c)        (((c) == '"'))
#define CHAR_IS_SPACE(c)        (((c) == ' ') || ((c) == '\t'))
#define IDX_LINE_SIZE 128
#define TAP_HEADER_SIZE 20

static FIL idxfile;
static FIL tapfile;
static int idx_pt = 0;
static int lastidx = -1;
static char idxline[IDX_LINE_SIZE];
static int f_index;
static int empty_counter = 1;

static char idx_getch()
{
	UINT br;
	if (!(idx_pt&0x1ff)) {
		f_read(&idxfile, sector_buffer, 512, &br);
	}
	if (idx_pt >= f_size(&idxfile)) return 0;
	else return sector_buffer[(idx_pt++)&0x1ff];
}

static int idx_getline(char* line, int *offset)
{
	char c;
	char ignore=0;
	char literal=0;
	char leadingspace = 0;
	int i=0;
	*offset = 0;

	while(1) {
		c = idx_getch();
		int is_end = ((!c) || CHAR_IS_LINEEND(c));
		int is_separator = (*offset == 0 && CHAR_IS_SPACE(c));

		if (is_separator || (is_end && *offset == 0 && i > 0)) {
			line[i] = 0;
			*offset = (int)strtoll(line, NULL, 0);
			i = 0;
			line[0] = '\0';
			if (*offset) leadingspace = 1;
			if (is_end) break;
			continue;
		}

		if (is_end) break;
		if (!CHAR_IS_SPACE(c) && *offset) leadingspace = 0;
		if (CHAR_IS_QUOTE(c) && !ignore) literal ^= 1;
		else if (CHAR_IS_COMMENT(c) && !ignore && !literal) ignore++;
		else if ((literal || !ignore ) && i<(IDX_LINE_SIZE-1) && !leadingspace) line[i++] = c;
		if (*offset == 0 && CHAR_IS_SPACE(c)) {
			line[i] = 0;
			*offset = strtoll(line, NULL, 0);
			i = 0;
			line[0] = '\0';
			if (*offset) leadingspace = 1;
		}
	}
	line[i] = '\0';

	if (*offset != 0) {
		int j;
		for (j = 0; line[j] && CHAR_IS_SPACE(line[j]); j++);
		if (line[j] == '\0') {
			sprintf(line, "NO-NAME-%03d", empty_counter++);
		}
	}

	return c==0 ? IDX_EOT : literal ? 1 : 0;
}

static char *idxitem(int idx, int *offset)
{
	if (idx <= lastidx) {
		idx_pt = 0;
		f_rewind(&idxfile);
		lastidx = -1;
		empty_counter = 1;
	}
	while (1) {
		int r = idx_getline(idxline, offset);
		if (idxline[0]) lastidx++;
		if (r == IDX_EOT || idx == lastidx) break;
	}
	return idxline;
}

static char idx_getmenupage(uint8_t idx, char action, menu_page_t *page) 
{
	if (action == MENU_PAGE_EXIT) {
		f_close(&idxfile);
		f_close(&tapfile);
		debug_idx_close();  // << DEBUG: Close log on exit
		return 0;
	}
	page->title = "IDX";
	page->flags = 0;
	page->timer = 0;
	page->stdexit = MENU_STD_EXIT;
	return 0;
}

static char idx_getmenuitem(uint8_t idx, char action, menu_item_t *item)
{
	int offset, next_offset;
	char *str;
	char current_name[IDX_LINE_SIZE];

	if (action == MENU_ACT_GET) {
		str = idxitem(idx, &offset);
		item->item = str;
		item->active = (str[0] != 0);
		return (str[0] != 0);
		
	} else if (action == MENU_ACT_SEL) {
		DEBUG_IDX_START("IDX LOAD");  // << DEBUG
		
		str = idxitem(idx, &offset);
		strcpy(current_name, str);
		debug_idx_log("Program: '%s' (index %d)\n", str, idx);        // << DEBUG
		debug_idx_log("Offset: 0x%08X (%u)\n", offset, offset);       // << DEBUG

		FSIZE_t total_size = f_size(&tapfile);
		debug_idx_log("TAP size: 0x%08X (%u)\n",                      // << DEBUG
		             (unsigned int)total_size, (unsigned int)total_size);
		
		if ((FSIZE_t)offset >= total_size) {
			debug_idx_log("ERROR: Invalid offset\n");  // << DEBUG
			f_close(&tapfile);
			CloseMenu();
			return 1;
		}

		idxitem(idx + 1, &next_offset);
		debug_idx_log("Next offset: 0x%08X\n", next_offset);  // << DEBUG
		
		iprintf("IDX: load \"%s\" at 0x%08x\n", str, offset);
		f_close(&idxfile);

		UINT br;
		FRESULT res;
		FSIZE_t program_size;

		debug_idx_subsection("Read TAP header");  // << DEBUG
		DISKLED_ON
		res = f_read(&tapfile, sector_buffer, TAP_HEADER_SIZE, &br);
		DISKLED_OFF
		
		if (res != FR_OK || br != TAP_HEADER_SIZE) {
			debug_idx_log("ERROR: Header read failed\n");  // << DEBUG
			f_close(&tapfile);
			CloseMenu();
			return 1;
		}
		
		debug_idx_log_hex("TAP header", sector_buffer, TAP_HEADER_SIZE);  // << DEBUG
		
		unsigned int orig_size = 
			(sector_buffer[0x13] << 24) | (sector_buffer[0x12] << 16) |
			(sector_buffer[0x11] <<  8) | sector_buffer[0x10];
		debug_idx_log("Original size: 0x%08X\n", orig_size);  // << DEBUG

		debug_idx_subsection("Calculate size");  // << DEBUG
		FSIZE_t end_pos = (next_offset > offset) ? (FSIZE_t)next_offset : total_size;
		program_size = end_pos - (FSIZE_t)offset;
		debug_idx_log("Start: 0x%08X, End: 0x%08X\n",                 // << DEBUG
		             offset, (unsigned int)end_pos);
		debug_idx_log("Program size: 0x%08X (%u)\n",                  // << DEBUG
		             (unsigned int)program_size, (unsigned int)program_size);

		debug_idx_subsection("Update header");  // << DEBUG
		sector_buffer[0x10] = (program_size >>  0) & 0xFF;
		sector_buffer[0x11] = (program_size >>  8) & 0xFF;
		sector_buffer[0x12] = (program_size >> 16) & 0xFF;
		sector_buffer[0x13] = (program_size >> 24) & 0xFF;
		debug_idx_log("New size bytes: %02X %02X %02X %02X\n",        // << DEBUG
		             sector_buffer[0x10], sector_buffer[0x11], 
		             sector_buffer[0x12], sector_buffer[0x13]);

		debug_idx_log_hex("New TAP header", sector_buffer, TAP_HEADER_SIZE);  // << DEBUG
		
		debug_idx_subsection("Send header to FPGA");  // << DEBUG
		data_io_file_tx_prepare(&tapfile, f_index, "TAP");
		EnableFpga();
		SPI(DIO_FILE_TX_DAT);
		spi_write(sector_buffer, TAP_HEADER_SIZE);
		DisableFpga();
		debug_idx_log("Header sent (%d bytes)\n", TAP_HEADER_SIZE);  // << DEBUG

		debug_idx_subsection("Send data");  // << DEBUG
		if (f_lseek(&tapfile, offset) == FR_OK) {
			FSIZE_t bytes_to_send = program_size;
			unsigned int chunk = 0, total_sent = 0;
			
			while(bytes_to_send > 0) {
				UINT bytes_to_read = (bytes_to_send > SECTOR_BUFFER_SIZE) ? 
							 SECTOR_BUFFER_SIZE : (UINT)bytes_to_send;

				DISKLED_ON
				res = f_read(&tapfile, sector_buffer, bytes_to_read, &br);
				DISKLED_OFF

				if (res != FR_OK || br == 0) {
					debug_idx_log("ERROR: Read failed (res=%d, br=%d)\n", res, br);  // << DEBUG
					break;
				}
 
				// << DEBUG: Analyze first chunk
				if (chunk == 0) {
					debug_idx_log("\nFirst chunk analysis:\n");
					debug_idx_log_hex("First 32 bytes", sector_buffer, (br > 32) ? 32 : br);
					
					// Detailed analysis of first bytes
					debug_idx_log("\nByte-by-byte analysis:\n");
					int bytes_to_analyze = (br > 16) ? 16 : br;
					for (int i = 0; i < bytes_to_analyze; i++) {
						unsigned char b = sector_buffer[i];
						debug_idx_log("[%02d]=0x%02X (%3d)", i, b, b);
						
						if (b >= 0x28 && b <= 0x3C) {
							debug_idx_log(" PILOT/PAD (pulse ~%d cycles)", b * 8);
						} else if (b == 0x00) {
							debug_idx_log(" OVERFLOW");
						}
						debug_idx_log("\n");
					}
				}
 
				EnableFpga();
				SPI(DIO_FILE_TX_DAT);
				spi_write(sector_buffer, br);
				DisableFpga();
 
				total_sent += br;
				bytes_to_send -= br;
				
				// << DEBUG: Progress every 10 chunks
				if (chunk % 10 == 0) {
					debug_idx_log("Chunk %d: %u bytes (total %u)\n", chunk, br, total_sent);
				}
				chunk++;
			}

			debug_idx_log("\nTransfer complete: %u bytes in %d chunks\n",  // << DEBUG
			             total_sent, chunk);
		} else {
			debug_idx_log("ERROR: Seek failed\n");  // << DEBUG
		}

		data_io_file_tx_done();
		f_close(&tapfile);
		DEBUG_IDX_END("IDX LOAD");  // << DEBUG
		
		CloseMenu();
		return 1;
	}
	return 0;
}

static void handleidx(FIL *file, int index, const char *name, const char *ext)
{
	debug_idx_enable();  // << DEBUG: Enable idx logging
	DEBUG_IDX_START("IDX FILE OPEN");  // << DEBUG
	debug_idx_log("File: %s\n", name);  // << DEBUG

	iprintf("IDX: open IDX %s\n", name);

	empty_counter = 1;
	f_rewind(file);
	idxfile = *file;
	idx_pt = 0;
	lastidx = -1;
	f_index = index;

	const char *fileExt = 0;
	int len = strlen(name);
	while(len > 2) {
		if (name[len-2] == '.') {
			fileExt = &name[len-1];
			break;
		}
		len--;
	}
	
	if (fileExt) {
		char tap[len+3];
		memcpy(tap, name, len-1);
		strcpy(&tap[len-1], "TAP");
		
		debug_idx_log("TAP file: %s\n", tap);  // << DEBUG
		
		if(f_open(&tapfile, tap, FA_READ) != FR_OK) {
			debug_idx_log("ERROR: Cannot open TAP\n");  // << DEBUG
			f_close(&idxfile);
			ErrorMessage("Unable to open the\ncorresponding TAP file!", 0);
			return;
		}
		
		FSIZE_t tap_size = f_size(&tapfile);
		debug_idx_log("TAP opened: %u bytes\n", (unsigned int)tap_size);  // << DEBUG
		
		// << DEBUG: List all IDX entries
		debug_idx_log("\nIDX entries:\n");
		int temp_offset, entry = 0;
		idx_pt = 0;
		f_rewind(&idxfile);
		lastidx = -1;
		empty_counter = 1;
		
		while(entry < 20) {
			char *e = idxitem(entry, &temp_offset);
			if (e[0] == 0) break;
			debug_idx_log("  %2d: 0x%08X  \"%s\"\n", entry, temp_offset, e);
			entry++;
		}
		
		// Reset for actual use
		idx_pt = 0;
		f_rewind(&idxfile);
		lastidx = -1;
		empty_counter = 1;
		
		SetupMenu(&idx_getmenupage, &idx_getmenuitem, NULL);
		debug_idx_log("Menu ready\n");  // << DEBUG
		debug_idx_log("====================================\n\n");  // << DEBUG
	} else {
		debug_idx_log("ERROR: Invalid extension\n");  // << DEBUG
		f_close(&idxfile);
		CloseMenu();
	}
}

static data_io_processor_t idx_file = {"IDX", &handleidx};

void idx_files_init()
{
	data_io_add_processor(&idx_file);
}
