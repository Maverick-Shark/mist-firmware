/*
  This file is part of MiST-firmware

  MiST-firmware is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  MiST-firmware is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>

#include "idx_files.h"
#include "fat_compat.h"
#include "data_io.h"
#include "menu.h"
#include "osd.h"

#define IDX_EOT                 4 // End-Of-Transmission

#define CHAR_IS_LINEEND(c)      (((c) == '\n'))
#define CHAR_IS_COMMENT(c)      (((c) == ';'))
#define CHAR_IS_QUOTE(c)        (((c) == '"'))
#define CHAR_IS_SPACE(c)        (((c) == ' ') || ((c) == '\t'))

#define IDX_LINE_SIZE 128
#define TAP_HEADER_SIZE 20        // TAP header: bytes 0x00-0x13 (20 bytes)

static FIL idxfile;
static FIL tapfile;
static int idx_pt = 0;
static int lastidx = -1;
static char idxline[IDX_LINE_SIZE];
static int f_index;
static int empty_counter = 1;                   // Counter for empty program names

static char idx_getch()
{
	UINT br;

	if (!(idx_pt&0x1ff)) {
		// reload buffer
		f_read(&idxfile, sector_buffer, 512, &br);
		//hexdump(sector_buffer, 512, 0);
	}

	if (idx_pt >= f_size(&idxfile)) return 0;
	else return sector_buffer[(idx_pt++)&0x1ff];
}

static int idx_getline(char* line, int *offset)
{
	char c;
	char ignore=0;          // Flag to ignore comments
	char literal=0;         // Flag for literal strings (quoted)
	char leadingspace = 0;  // Flag for leading spaces after offset
	int i=0;
	int j;
	*offset = 0;

	while(1) {
		c = idx_getch();
		// Break on newline or EOF
		if ((!c) || CHAR_IS_LINEEND(c)) break;

		// Clear leading space flag when non-space character found
		if (!CHAR_IS_SPACE(c) && *offset) leadingspace = 0;

		// Handle quoted strings
		if (CHAR_IS_QUOTE(c) && !ignore) literal ^= 1;
		// Handle comments (lines starting with ';')
		else if (CHAR_IS_COMMENT(c) && !ignore && !literal) ignore++;
		// Add character to output if not ignored
		else if ((literal || !ignore ) && i<(IDX_LINE_SIZE-1) && !leadingspace) line[i++] = c;

		// Parse offset value (first field before space)
		if (*offset == 0 && CHAR_IS_SPACE(c)) {
			line[i] = 0;
			*offset = strtoll(line, NULL, 0);   // Convert hex string to integer
			i = 0;                              // Restart counter to read the name
			if (*offset) leadingspace = 1;
		}
	}
	line[i] = '\0';          // String ends
	
        // Check if name is empty or contains only spaces
        if (*offset != 0) {      // Only check if parsed an offset
            int is_empty = 1;
            for (j = 0; line[j]; j++) {
                if (line[j] != ' ' && line[j] != '\t') {
                    is_empty = 0;
                    break;
                }
            }

            // If name is empty, replace with "NO-NAME-XXX" string
            if (is_empty || line[0] == '\0') {
                sprintf(line, "NO-NAME-%03d", empty_counter++);
            }
        }

	return c==0 ? IDX_EOT : literal ? 1 : 0;
}

static char *idxitem(int idx, int *offset)
{
	// Rewind if requested index is before current position
	if (idx <= lastidx) {
		idx_pt = 0;
		f_rewind(&idxfile);
		lastidx = -1;
		empty_counter = 1;      // Reset counter to ensure consistent names
	}
	// Read lines until we reach the requested index
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
	int offset;             // TAP file offset from IDX file
	char *str;              // Program name string

	// ACTION: Get item for menu display
	if (action == MENU_ACT_GET) {
		str = idxitem(idx, &offset);    // Read each .idx line
		item->item = str;               // Pass name to the OSD
		item->active = (str[0] != 0);
		return (str[0] != 0);
	// ACTION: User selected this menu item
	} else if (action == MENU_ACT_SEL) {
		str = idxitem(idx, &offset);
		iprintf("IDX: load TAP segment \"%s\" at offset 0x%08x\n", str, offset);
		// Load TAP from the offset
		f_close(&idxfile);

		UINT br;
		FRESULT res;
		FSIZE_t total_size, remaining_size;

		// Read original TAP header (20 bytes: 0x00-0x13)
		DISKLED_ON
		res = f_read(&tapfile, sector_buffer, TAP_HEADER_SIZE, &br);
		DISKLED_OFF
		if (res != FR_OK || br != TAP_HEADER_SIZE) {
			f_close(&tapfile);
			CloseMenu();
			iprintf("IDX: Error reading TAP header\n");
			return 1;
		}

		// Calculate remaining data size from offset to end of file
		total_size = f_size(&tapfile);
		iprintf("IDX: offset: %d bytes, total_size size: %d bytes\n", sizeof(offset), sizeof(total_size));

		// Verify that offset is not greater than file size
		// Convert offset to FSIZE_t type
		if ((FSIZE_t)offset >= total_size) {
			f_close(&tapfile);
			CloseMenu();
			iprintf("IDX: Invalid offset 0x%08x (file size: 0x%08x)\n", offset, (unsigned int)total_size);
			return 1;
		}

		// Calculate remaining data size from offset to EOF
		remaining_size = total_size - (FSIZE_t)offset;

		iprintf("IDX: Total file size: 0x%08x, Offset: 0x%08x, Remaining: 0x%08x\n", 
		        (unsigned int)total_size, offset, (unsigned int)remaining_size);

		// Update size field in TAP header
		// Bytes 0x10-0x13 (16-19 in decimal) contain the size in little-endian
		sector_buffer[0x10] = (remaining_size >>  0) & 0xFF;
		sector_buffer[0x11] = (remaining_size >>  8) & 0xFF;
		sector_buffer[0x12] = (remaining_size >> 16) & 0xFF;
		sector_buffer[0x13] = (remaining_size >> 24) & 0xFF;

		// Prepare and send corrected TAP header
		data_io_file_tx_prepare(&tapfile, f_index, "TAP");
		EnableFpga();
		SPI(DIO_FILE_TX_DAT);
		spi_write(sector_buffer, TAP_HEADER_SIZE);
		DisableFpga();

		// Position file at specified offset
		if (f_lseek(&tapfile, offset) == FR_OK) {
			iprintf("IDX: Sending TAP data from offset 0x%08x...\n", offset);
			// Read and send data from offset to end
			while(1) {
				DISKLED_ON
				res = f_read(&tapfile, sector_buffer, SECTOR_BUFFER_SIZE, &br);
				DISKLED_OFF
				if (res == FR_OK && br > 0) {
					EnableFpga();
					SPI(DIO_FILE_TX_DAT);
					spi_write(sector_buffer, br);
					DisableFpga();
				}
				if (res != FR_OK || br != SECTOR_BUFFER_SIZE) break;
			}
			iprintf("IDX: TAP data transmission complete\n");
		} else {
			iprintf("IDX: Error seeking to offset 0x%08x\n", offset);
		}

		data_io_file_tx_done();
		f_close(&tapfile);
		CloseMenu();
		return 1;
	} else
		return 0;
}

static void handleidx(FIL *file, int index, const char *name, const char *ext)
{
	iprintf("IDX: open IDX %s\n", name);

	empty_counter = 1;    // Reset empty name counter
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
		if(f_open(&tapfile, tap, FA_READ) != FR_OK) {
			f_close(&idxfile);
			ErrorMessage("Unable to open the\ncorresponding TAP file!", 0);
			return;
		}
		SetupMenu(&idx_getmenupage, &idx_getmenuitem, NULL);
	} else {
		f_close(&idxfile);
		CloseMenu();
	}
}

static data_io_processor_t idx_file = {"IDX", &handleidx};


void idx_files_init()
{
	data_io_add_processor(&idx_file);
}
