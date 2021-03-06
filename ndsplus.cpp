/*
Copyright 2012 Jaron Vietor

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
License for more details.
You should have received a copy of the GNU General Public License along with this program.
If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "libusb.h"

/// Outputs a 24-byte aligned hexdump to stderr.
void hexdump(unsigned char *p, int len){
  for (int i = 0; i < len; i += 24){
    fprintf(stderr, "%04x: ", i);
    for (int j = 0; j < 24; ++j){
      if (j % 4 == 3){
        if (i+j < len){fprintf(stderr, "%02x ", p[i+j]);}else{fprintf(stderr, "   ");}
      }else{
        if (i+j < len){fprintf(stderr, "%02x", p[i+j]);}else{fprintf(stderr, "  ");}
      }
      fprintf(stderr, " ");
    }
    for (int j = 0; j < 24; ++j){
      if (i+j < len){
        if (isprint(p[i+j])){fprintf(stderr, "%c", p[i+j]);}else{fprintf(stderr, ".");}
      }
    }
    fprintf(stderr, "\n");
  }
}

// Globals are nasty, I know.
libusb_device_handle * handle = 0;
uint8_t send_addr = 0;
uint8_t recv_addr = 0;

/// Releases the adapter interface and closes the handle.
/// Prints the given error message and returns 1.
int drop_adapter(const char * error){
  std::cout << error << std::endl;
  libusb_release_interface(handle, 0);
  libusb_close(handle);
  return 1;
}

/// Retries the status of the NDS Adapter and returns the 8 status bytes.
/// The status bytes are like this: ff ff0000 00 aa 3001 (no card inserted)
/// The first five bytes relate to the savedata chip.
/// The first byte identifies the chip type, and size for EEPROM games.
/// The second byte holds the EEPROM busy bit (bit0), 1 meaning busy.
/// The fifth byte is the savegame size in bytes, encoded as (1 << X).
/// The sixth byte is unused and seems to always be 0xAA.
/// The last two bytes are the firmware version in little endian format.
unsigned char * get_status(){
  unsigned char get_status[10] = {0x9c, 0xa5, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};
  static unsigned char status_reply[8];
  int len = 0;
  int ret = 0;
  ret = libusb_bulk_transfer(handle, send_addr, get_status, 10, &len, 1000);
  if (ret == 0 && len == 10){
    ret = libusb_bulk_transfer(handle, recv_addr, status_reply, 8, &len, 1000);
    if (ret == 0 && len == 8){
      return status_reply;
    }else{
      std::cout << "Error reading status response: " << ret << "," << len << std::endl;
    }
  }else{
    std::cout << "Error requesting status: " << ret << "," << len << std::endl;
  }
  return 0;
}

/// This function prepares the card for reading the header
bool prepare_card(){
  int len = 0;
  int ret = 0;
  unsigned char request[10] = {0, 0xa5, 0, 0, 0, 0, 0, 0, 0, 0};
  unsigned char response[4] = {0, 0, 0, 0};

  //9fa5 9f000000 00 00 0000?
  request[0] = 0x9f; request[2] = 0x9f;
  ret = libusb_bulk_transfer(handle, send_addr, request, 10, &len, 1000);
  if (ret != 0 || len != 10){
    std::cout << "Could not send data_1 to card!" << std::endl;
    return false;
  }

  //90a5 90000000 00 00 0000?
  request[0] = 0x90; request[2] = 0x90;
  ret = libusb_bulk_transfer(handle, send_addr, request, 10, &len, 1000);
  if (ret != 0 || len != 10){
    std::cout << "Could not send data_2 to card!" << std::endl;
    return false;
  }
  
  //reply[4] = c2ff01c0
  ret = libusb_bulk_transfer(handle, recv_addr, response, 4, &len, 1000);
  if (ret != 0 || len != 4){
    std::cout << "Could not receive data from card!" << std::endl;
    return false;
  }
  return true;
}

/// Retrieves the first 512 bytes of the inserted card ROM and returns it.
/// This contains the beginning of the NDS header, and thus a lot of useful information.
unsigned char * get_header(){
  int len = 0;
  int ret = 0;
  unsigned char request[10] = {0, 0xa5, 0, 0, 0, 0, 0, 0, 0, 0};
  static unsigned char response[512];

  //00a5 00000000 00 00 0000?
  ret = libusb_bulk_transfer(handle, send_addr, request, 10, &len, 1000);
  if (ret != 0 || len != 10){
    std::cout << "Could not send info request to card!" << std::endl;
    return 0;
  }
  
  //reply[512] = beginning ROM header
  ret = libusb_bulk_transfer(handle, recv_addr, response, 512, &len, 1000);
  if (ret != 0 || len != 512){
    std::cout << "Could not receive info data from card!" << std::endl;
    return 0;
  }
  return response;
}

/// Retrieves 512 bytes of savegame data and returns it.
/// The data is retrieved from the given offset and bytepos.
/// offset is the first byte from the get_status() response.
/// bytepos is the byte-position where the 512 bytes that will be returned should start.
unsigned char * get_save(unsigned char offset, unsigned int bytepos){
  int len = 0;
  int ret = 0;
  unsigned char request[10] = {0x2c, 0xa5, 0, 0, 0, 0, 0x02, 0, 0, 0};
  //set the offset in the request
  request[7] = offset;
  //set the bytepos in the request
  request[2] = bytepos & 0xFF;
  request[3] = (bytepos >> 8) & 0xFF;
  request[4] = (bytepos >> 16) & 0xFF;
  request[5] = (bytepos >> 24) & 0xFF;
  static unsigned char response[512];

  //2ca5 00000000 02 13 0000?
  
  //2ca5 is the "command" for backup
  //the next 4 bytes are the address in little endian format
  //the seventh byte is always 0x02 - unknown what it is
  //the eight byte is the offset: the first byte of the get_status() response
  //the last two bytes seem to always be 0x0000
  ret = libusb_bulk_transfer(handle, send_addr, request, 10, &len, 10000);
  if (ret != 0 || len != 10){
    std::cout << "Could not send savegame request to card!" << std::endl;
    return 0;
  }
  
  //reply[512] = savegame data
  ret = libusb_bulk_transfer(handle, recv_addr, response, 512, &len, 10000);
  if (ret != 0 || len != 512){
    std::cout << "Could not receive savegame from card!" << std::endl;
    return 0;
  }
  return response;
}

/// Writes 256 bytes of savegame data.
/// The data is retrieved from the given offset and bytepos.
/// offset is the first byte from the get_status() response.
/// bytepos is the byte-position where the 256 bytes that will be returned should start.
int put_save(unsigned char offset, unsigned int bytepos, unsigned char * data){
  int len = 0;
  int ret = 0;
  unsigned char request[10] = {0x7b, 0xa5, 0, 0, 0, 0, 0x02, 0, 0, 0};
  //set the offset in the request
  request[7] = offset;
  //set the bytepos in the request
  request[2] = bytepos & 0xFF;
  request[3] = (bytepos >> 8) & 0xFF;
  request[4] = (bytepos >> 16) & 0xFF;
  request[5] = (bytepos >> 24) & 0xFF;

  if (offset == 0x93 || offset == 0x53 || offset == 0xA3){
    //the "command" for erase - needed for offsets 0x93, 0x53 and 0xA3
    if (offset == 0x93){
      request[0] = 0x5b;
    }else{
      request[0] = 0x5e;
    }
    ret = libusb_bulk_transfer(handle, send_addr, request, 10, &len, 10000);
    if (ret != 0 || len != 10){
      std::cout << "Warning: could not send erase command to card." << std::endl;
    }
    request[0] = 0x7b;
  }

  //7ba5 00000000 02 13 0000?
  
  //7ba5 is the "command" for restore
  //the next 4 bytes are the address in little endian format
  //the seventh byte is always 0x02 - unknown what it is
  //the eight byte is the offset: the first byte of the get_status() response
  //the last two bytes seem to always be 0x0000
  ret = libusb_bulk_transfer(handle, send_addr, request, 10, &len, 10000);
  if (ret != 0 || len != 10){
    std::cout << "Could not send savegame request to card!" << std::endl;
    return 0;
  }
  
  //request[256] = savegame data
  ret = libusb_bulk_transfer(handle, send_addr, data, 256, &len, 10000);
  if (ret != 0 || len != 256){
    std::cout << "Could not send savegame to card!" << std::endl;
    return 0;
  }
  return 1;
}

int main(int argc, char **argv){
  // set default options
  bool backup = false;
  bool restore = false;
  bool wipe = false;
  bool debug = false;
  unsigned int forced_size = 0;
  std::string backup_filename;
  std::string restore_filename;

  // parse options
  int opt = 1;
  while (opt != -1){
    static struct option long_options[] = {
      {"backup",  required_argument, 0, 'b'},
      {"restore", required_argument, 0, 'r'},
      {"wipe",    no_argument,       0, 'w'},
      {"size",    required_argument, 0, 's'},
      {"debug",   no_argument,       0, 'd'},
      {"help",    no_argument,       0, 'h'},
      {0,                   0,       0, 0  }
    };
    opt = getopt_long(argc, argv, "b:r:ws:dh", long_options, 0);
    switch (opt){
      case 'b':
        backup = true;
        backup_filename = optarg;
        break;
      case 'r':
        restore = true;
        restore_filename = optarg;
        break;
      case 's':
        forced_size = atoi(optarg);
        break;
      case 'w':
        wipe = true;
        break;
      case 'd':
        debug = true;
        break;
      case 'h':
      case '?':
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl << std::endl;
        std::cout << " --backup <file>,  -b <file> - Backs up savegame to file" << std::endl;
        std::cout << " --restore <file>  -r <file> - Restores savegame from file" << std::endl;
        std::cout << " --wipe            -w        - Wipes savegame on card" << std::endl;
        std::cout << " --size            -s <num>  - Force a savegame size, in bytes" << std::endl;
        std::cout << " --debug           -d        - Enables stderr raw hex output" << std::endl;
        std::cout << " --help            -h        - Output this message" << std::endl;
        return 1;
        break;
    }
  }

  // initialize libusb
  if (libusb_init(NULL)){
    std::cout << "Could not initialize libusb." << std::endl;
    return 1;
  }

  // attempt to find the NDS adapter by USB VID and PID.
  libusb_device **devs = 0;
  libusb_device *dev = 0;
  ssize_t num_devs;
  num_devs = libusb_get_device_list(NULL, &devs);
  if (num_devs < 1){
    std::cout << "Could not find any devices." << std::endl;
    return 1;
  }
  int i = 0;
  bool found = false;
  while ((dev = devs[i++]) != NULL) {
    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(dev, &desc);
    unsigned short dev_vid = desc.idVendor;
    unsigned short dev_pid = desc.idProduct;
    if (dev_vid == 0x4670 && dev_pid == 0x9394){
      if (libusb_open(dev, &handle) < 0){
        std::cout << "Found NDS Adapter+ but could not open! :-(" << std::endl;
        libusb_free_device_list(devs, 1);
        return 1;
      }
      struct libusb_config_descriptor *conf_desc = NULL;
      libusb_get_active_config_descriptor(dev, &conf_desc);
      send_addr = conf_desc->interface[0].altsetting[0].endpoint[1].bEndpointAddress;
      recv_addr = conf_desc->interface[0].altsetting[0].endpoint[0].bEndpointAddress;
      found = true;
    }
  }
  libusb_free_device_list(devs, 1);

  // cancel if adapter could not be found
  if (!found){
    std::cout << "Could not find NDS Adapter+ - is it plugged in?" << std::endl;
    return 1;
  }

  // attempt to claim the interface, cancel on error
  if (libusb_claim_interface(handle, 0) < 0){
    libusb_close(handle);
    std::cout << "Could not claim NDS Adapter+ - do you have access to the device?" << std::endl;
    return 1;
  }

  // retrieve the card status, hexdump it if debug is on, then print the firmware version
  unsigned char * card_status = get_status();
  if (card_status && debug){hexdump(card_status, 8);}
  std::cout << "NDS Adapter+ firmware version " << (card_status[7] * 256 + card_status[6]) << " detected." << std::endl;

  // if no card is plugged in, print this and abort
  if (!card_status || (card_status[0] == 0xFF && card_status[1] == 0xFF)){
    return drop_adapter("No card is plugged in.");
  }

  // prepare the card, abort if anything goes awry.
  if (!prepare_card()){
    return drop_adapter("Error preparing card.");
  }

  // retrieve the first 512 bytes of the card header and hexdump if debug is on
  // abort if anything went wrong
  unsigned char * card_header = get_header();
  if (card_header && debug){hexdump(card_header, 512);}
  if (!card_header){return drop_adapter("Read error.");}
  
  // print card details if available
  if (card_header[0x0] == 0xFF){
    //unreadable header
    printf("Card title: ???\n");
    printf("Card ID: ???\n");
    printf("Card size: ??? MiB\n");
  }else{
    //extract some interesting bits of info
    printf("Card title: %.12s\n", card_header);
    printf("Card ID: %.4s\n", card_header+0x00C);
    printf("Card size: %i MiB\n", (1 << (card_header[0x014] - 3)));
  }

  //check if this is a weird one or not
  if (card_status[0x04] > 0x20 && card_status[0x04] < 0xFF){
    return drop_adapter("It looks like you got a card that I don't know how to handle. Please run me with --debug and post the output as an issue on github - thanks!");
  }

  //calculate and print save size
  unsigned int save_size = 0;
  switch (card_status[0]){
    case 0x01:
      save_size = 512;
      printf("Detected save: 0.5 KiB EEPROM\n");
      break;
    case 0x02:
      save_size = 8192;
      printf("Detected save: 8 KiB EEPROM\n");
      break;
    case 0x12:
      save_size = 65536;
      printf("Detected save: 64 KiB EEPROM\n");
      break;
    default:
      save_size = 1 << card_status[0x04];
      printf("Detected save: %i KiB FLASH\n", save_size >> 10);
      break;
  }
  
  if (forced_size > 0){
    save_size = forced_size;
    printf("Overriding to: %i KiB\n", save_size >> 10);
  }

  // do a backup if requested - abort if file cannot be opened
  if (backup){
    printf("Backing up savegame...\n");
    std::ofstream backup_file(backup_filename.c_str(), std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);
    if (!backup_file.good()){return drop_adapter("Could not open file for backup :-(");}
    for (unsigned int i = 0; i < save_size; i += 512){
      unsigned char * data = get_save(card_status[0], i);
      if (!data){return drop_adapter("Savegame read error mid-operation!");}
      backup_file.write((const char*)data, 512);
      if (!backup_file.good()){return drop_adapter("File write error mid-operation!");}
      printf("\r%i%%...", (i*100) / save_size);
      fflush(stdout);
      usleep(1000);//sleep for 1ms - too fast makes the adapter unhappy :-(
    }
    printf("\r100%%   \nBackup to %s completed!\n", backup_filename.c_str());
  }

  // allocate a buffer for writing
  unsigned char put_buffer[256];

  // do a wipe if requested - abort if anything goes wrong
  if (wipe){
    printf("Wiping savegame...\n");
    memset(put_buffer, 0, 256);
    for (unsigned int i = 0; i < save_size; i += 256){
      int ret = put_save(card_status[0], i, put_buffer);
      if (!ret){return drop_adapter("Savegame write error mid-operation!");}
      printf("\r%i%%...", (i*100) / save_size);
      fflush(stdout);
      usleep(1000);//sleep for 1ms - too fast makes the adapter unhappy :-(
    }
    printf("\r100%%   \nWipe completed!\n");
  }

  // do a restore if requested - abort if anything goes wrong
  if (restore){
    printf("Restoring savegame...\n");
    std::ifstream restore_file(restore_filename.c_str(), std::ifstream::in | std::ifstream::binary);
    if (!restore_file.good()){return drop_adapter("Could not open file for restoring :-(");}
    for (unsigned int i = 0; i < save_size; i += 256){
      restore_file.read((char*)put_buffer, 256);
      if (!restore_file.good()){return drop_adapter("File read error mid-operation!");}
      int ret = put_save(card_status[0], i, put_buffer);
      if (!ret){return drop_adapter("Savegame write error mid-operation!");}
      printf("\r%i%%...", (i*100) / save_size);
      fflush(stdout);
      usleep(1000);//sleep for 1ms - too fast makes the adapter unhappy :-(
    }
    printf("\r100%%   \nRestore from file %s completed!\n", restore_filename.c_str());
  }

  libusb_release_interface(handle, 0);
  libusb_close(handle);
  return 0;
}


//get_status and get_unknown values for some games:
//ff ff0000 00aa3001 <- no card
//13 002040 13aa3001 <- pkmn black 1 (4mbit / 256 rom)    c2 ff 01 c0
//23 002040 14aa3001 <- walking trainer (8mbit / 64m rom) 80 3f 01 e0
//93 00c222 11aa3001 <- pilotwings (1mbit / ??? rom)      c2 7f 00 90
//93 00c222 11aa3001 <- mario 3ds (1mbit / ??? rom)       c2 fe 00 90
//01 f0ffff ffaa3001 <- marchofpenguins (4kbit / 8m)      c2 07 00 00
//01 f0ffff ffaa3001 <- shrek super slam (4k / 32m)       c2 1f 00 00
