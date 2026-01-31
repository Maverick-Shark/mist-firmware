/*
  data_io.c - File transmission to FPGA

  Modified version with debug_io (tap/idx files)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "user_io.h"
#include "data_io.h"
#include "debug.h"
#include "spi.h"
#include "debug_io.h"  // << DEBUG SYSTEM
#ifdef HAVE_QSPI
#include "qspi.h"
#endif

// core supports direct ROM upload via SS4
char rom_direct_upload = 0;

static data_io_processor_t* PROCESSORS[MAX_DATA_IO_PROCESSORS];

void data_io_init() {
  memset(PROCESSORS, 0, sizeof(PROCESSORS));
}

void data_io_set_index(char index) {
  debug_io_log("Index: %d\n", index);              // << DEBUG
  
  EnableFpga();
  SPI(DIO_FILE_INDEX);
  SPI(index);
  DisableFpga();
}

void data_io_file_tx_start(void) {
  debug_io_log("File Tx Starts (0xFF)\n");         // << DEBUG
  
  EnableFpga();
  SPI(DIO_FILE_TX);
  SPI(0xff);
  DisableFpga();

#ifdef HAVE_QSPI
  if (user_io_get_core_features() & FEAT_QSPI)
    qspi_start_write();
#endif
}

void data_io_file_tx_done(void) {
  // signal end of transmission
  debug_io_log("File Tx Ends (0x00)\n");           // << DEBUG

  EnableFpga();
  SPI(DIO_FILE_TX);
  SPI(0x00);
  DisableFpga();

#ifdef HAVE_QSPI
  if (user_io_get_core_features() & FEAT_QSPI)
    qspi_end();
#endif
  iprintf("\n");
}

///////////////////////////
// TRANSMIT FILE TO FPGA //
///////////////////////////

void data_io_file_tx_prepare(FIL *file, char index, const char *ext) {
  char e[3];

  FSIZE_t fsize = file ? f_size(file) : 0;

  debug_io_enable(ext);                                  // << DEBUG: Enable logging for TAP files

  debug_io_section("FILE TX PREPARE");                   // << DEBUG
  debug_io_log("Index: %d, Ext: %s, Size: 0x%08X\n",     // << DEBUG
               index, ext ? ext : "NULL", (unsigned int)fsize);

  iprintf("Preparing transmission for index %d\n", index);

  e[0] = e[1] = e[2] = ' ';
  if (ext) {
    if (ext[0]) e[0] = toupper(ext[0]);
    if (ext[1]) e[1] = toupper(ext[1]);
    if (ext[2]) e[2] = toupper(ext[2]);
  }

  // set index byte (0=bios rom, 1-n=OSD entry index)
  data_io_set_index(index);

  EnableFpga();
  SPI(DIO_FILE_INFO);

  spi_n(0, 8);                      // name
  spi8(e[0]);spi8(e[1]);spi8(e[2]); // ext
  spi8(file->obj.attr);             // attr
  spi8(0);                          // unsigned char       LowerCase;          NT VFAT lower case flags
  spi8(0);                          // unsigned char       CreateHundredth;    hundredth of seconds in CTime
  spi16(0);                         // unsigned short      CreateTime;         create time
  spi16(0);                         // unsigned short      CreateDate;         create date
  spi16(0);                         // unsigned short      AccessDate;         access date
  spi16le(file->obj.sclust >> 16);  // unsigned short      HighCluster;        high bytes of cluster number
  spi16(0);                         // unsigned short      ModifyTime;         last update time
  spi16(0);                         // unsigned short      ModifyDate;         last update date
  spi16le(file->obj.sclust);        // unsigned short      StartCluster;       starting cluster of file
  spi32le(fsize);

  DisableFpga();

  // prepare transmission of new file
  data_io_file_tx_start();
  debug_io_log("File info sent to FPGA\n");   // << DEBUG
}

static void data_io_file_tx_send(FIL *file) {
  FSIZE_t bytes2send = f_size(file);
  UINT br;

  unsigned int chunk_num = 0;
  debug_io_section("FILE TX SEND");               // << DEBUG
  debug_io_log("Total bytes to send: 0x%08X\n",   // << DEBUG
              (unsigned int)bytes2send);          // << DEBUG
  debug_io_reset_spi_counter();                   // << DEBUG
  
  /* transmit the entire file using one transfer */
  iprintf("Selected %llu bytes to send\n", bytes2send);

  while(bytes2send) {
    iprintf(".");

    unsigned short c, chunk = (bytes2send>SECTOR_BUFFER_SIZE)?SECTOR_BUFFER_SIZE:bytes2send;
    char *p;

    if (rom_direct_upload && fat_uses_mmc()) {
      // upload directly from the SD-Card if the core supports that
      debug_io_log("Using direct ROM upload\n");   // << DEBUG
      bytes2send = (file->obj.objsize + 511) & 0xfffffe00;
      file->obj.objsize = bytes2send; // hack to foul FatFs think the last block is a full sector
      DISKLED_ON
      f_read(file, 0, bytes2send, &br);
      DISKLED_OFF
      bytes2send = 0;
    } else {
      DISKLED_ON
      f_read(file, sector_buffer, chunk, &br);
      DISKLED_OFF

      if (debug_io_is_enabled() && chunk_num == 0 && br >= 20) {
        debug_io_subsection("First Chunk");
        debug_io_analyze_tap_header(sector_buffer);
        debug_io_analyze_tap_data(sector_buffer + 20, br - 20, 20);
      }

#ifdef HAVE_QSPI
      if (user_io_get_core_features() & FEAT_QSPI) {
        debug_io_log_spi_write(sector_buffer, chunk, "QSPI");  // << DEBUG
        qspi_write_block(sector_buffer, chunk);
      } else {
#endif
        EnableFpga();
        SPI(DIO_FILE_TX_DAT);

        // Log the actual SPI write
        debug_io_log_spi_write(sector_buffer, chunk, "SPI");   // << DEBUG

        // Send data byte by byte (not using DMA for compatibility)
        for(p = sector_buffer, c=0; c < chunk; c++)
          SPI(*p++);

        DisableFpga();
#ifdef HAVE_QSPI
      }
#endif
      bytes2send -= chunk;

      chunk_num++;
    }
  }

  if (debug_io_is_enabled()) {   // << DEBUG: Final summary
    debug_io_log("\nTransmission complete: %d chunks, 0x%08X bytes\n", 
                chunk_num, debug_io_get_spi_counter());
  }
}


static void data_io_file_tx_fill(unsigned char fill, unsigned int len) {
  debug_io_log("Filling: 0x%02X x %u\n", fill, len);   // << DEBUG

  EnableFpga();
  SPI(DIO_FILE_TX_DAT);
  while(len--) {
    SPI(fill);
  }
  DisableFpga();
}

void data_io_file_tx(FIL *file, char index, const char *ext) {
  DEBUG_IO_START("FULL FILE TRANSFER STARTS");                       // << DEBUG
  debug_io_log("Index: %d, Ext: %s\n", index, ext ? ext : "NULL");   // << DEBUG

  data_io_file_tx_prepare(file, index, ext);
  data_io_file_tx_send(file);
  data_io_file_tx_done();

  DEBUG_IO_END("FULL FILE TRANSFER ENDS");                           // << DEBUG

  debug_io_close();   // << DEBUG: Close log after TAP transfer
}

static data_io_processor_t* data_io_get_processor(const char *processor_id) {
  for (int i = 0; i < MAX_DATA_IO_PROCESSORS; i++) {
    if (PROCESSORS[i]) {
      if (PROCESSORS[i]->id && strncmp(PROCESSORS[i]->id, processor_id, 3) == 0) {
        return PROCESSORS[i];
      }
    } else {
      break;
    }
  }
  return NULL;
}

char data_io_add_processor(data_io_processor_t *processor) {
  if (processor) {
    for (int i = 0; i < MAX_DATA_IO_PROCESSORS; i++) {
      if (!PROCESSORS[i]) {
        PROCESSORS[i] = processor;
        return 0;
      }
    }
  }
  return -1;
}

void data_io_file_tx_processor(FIL *file, char index, const char *ext, const char *name, const char *processor_id) {
  iprintf("data_io_file_tx_processor idx: %d ext: %s\n", index, ext);
  data_io_processor_t *processor;
  if (processor_id && (processor = data_io_get_processor(processor_id))) {
    debug_io_log("Using processor: %s\n", processor_id);   // << DEBUG
    processor->file_tx_send(file, index, name, ext);
  } else {
    iprintf("Processor for %s not found. Defaulting to normal upload\n", processor_id);
    debug_io_log("No processor found, using default upload\n");   // << DEBUG
    data_io_file_tx_prepare(file, index, ext);
    data_io_file_tx_send(file);
    data_io_file_tx_done();
  }
}

// send 'fill' byte 'len' times
void data_io_fill_tx(unsigned char fill, unsigned int len, char index) {
  data_io_file_tx_prepare(0, index, 0);
  data_io_file_tx_fill(fill, len);
  data_io_file_tx_done();
}

////////////////////////////
// RECEIVE FILE FROM FPGA //
////////////////////////////

static void data_io_file_rx_prepare(char index) {
  iprintf("Preparing receiving for index %d\n", index);

  // set index byte (0=bios rom, 1-n=OSD entry index)
  data_io_set_index(index);

  // prepare transmission of new file
  EnableFpga();
  SPI(DIO_FILE_RX);
  SPI(0xff);
  DisableFpga();
}

static void data_io_file_rx_receive(FIL *file, unsigned int len) {
  unsigned int bytes2receive = len;
  char first = 1;
  UINT bw;
  /* receive the entire file using one transfer */
  iprintf("Selected %lu bytes to receive\n", bytes2receive);

  while(bytes2receive) {
    iprintf(".");

    unsigned short c, chunk = (bytes2receive>2048)?2048:bytes2receive;
    char *p;

    EnableFpga();
    SPI(DIO_FILE_RX_DAT);
    if (first) {
      SPI(0);
      first=0;
    }

    for(p = sector_buffer, c=0;c < chunk;c++)
      *p++ = SPI(0xFF);

    DisableFpga();
    bytes2receive -= chunk;
    DISKLED_ON
    f_write(file, sector_buffer, chunk, &bw);
    DISKLED_OFF
  }
}

static void data_io_file_rx_done(void) {
  // signal end of transmission
  EnableFpga();
  SPI(DIO_FILE_RX);
  SPI(0x00);
  DisableFpga();

  iprintf("\n");
}

void data_io_file_rx(FIL *file, char index, unsigned int len) {
  data_io_file_rx_prepare(index);
  data_io_file_rx_receive(file, len);
  data_io_file_rx_done();
}

////////////////
// ROM UPLOAD //
////////////////

void data_io_rom_upload(char *rname, char mode) {

  FIL file;
  FRESULT res;
  static char first = 1;
  char fname[67];

  // new ini parsing starts, prepare uploads
  if(mode == 0) {
    first = 1;
    return;
  }

  // ini parsing done
  if(mode == 2) {
    // has something been uploaded?
    // -> then end transfer
    if(!first) {
      iprintf("upload ends\n");

      data_io_file_tx_done();
      user_io_8bit_set_status(0, UIO_STATUS_RESET);
    }
    return;
  }

  // try to change into core dir. Stay in root if that doesn't exist
  user_io_change_into_core_dir();

  strncpy(fname, rname, sizeof(fname)-5);
  fname[sizeof(fname)-6] = 0;
  strcat(fname,".ROM");
  iprintf("rom upload '%s'\n", fname);

  res=f_open(&file, fname, FA_READ);
  if (res== FR_OK) {

    if(first) {
      // set reset
      user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);
      data_io_file_tx_prepare(&file, 0, "ROM");
      first = 0;
    }

    //user_io_file_tx(&f, 0);
    data_io_file_tx_send(&file);
    f_close(&file);
  } else
    iprintf("Error opening file %s (%d)!\n", fname, res);

  ChangeDirectoryName("/");
  ScanDirectory(SCAN_INIT, "RBF",  SCAN_LFN | SCAN_SYSDIR);
}
