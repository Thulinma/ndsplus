Summary, disclaimer, license, etc
=================================

ndsplus will allow you to use an EMS NDS Adapter+ under linux natively, without needing to run their glitchy software in a VM or anything like that. It provides the same basic features as the official software - being backup and restore as well as a very brief summary of the inserted card.

I (Thulinma AKA Jaron Vietor) am the author of all the code included, which isn't all that much. I'm releasing this under the GPLv3 license - meaning you can do whatever you want with it, as long as you provide the source code of any changes. If you do make changes, please submit them as a pull request over github (or, if you hate git with a vengeance, e-mail me the changes or something). You don't _have_ to do this since GPL doesn't require notifying the original author of any modified versions, but I would really appreciate it and it helps provide a single place where the "most capable" version of this software will live - being right here.


Compiling
=========

Should be simple enough - just run make. You'll need at least pkg-config and libusb installed, nothing else should be required.

Preparing your system
=====================

A little more complicated - you need access to the USB device. The simplest way to do this is by running the program as root, but for obvious reasons that's not a good way to do things. The proper way is to create a file /etc/udev/rules.d/ndsadapterplus.rules with the following contents:

    SUBSYSTEM=="usb", ATTRS{idVendor}=="4670", ATTRS{idProduct}=="9394", GROUP="users", MODE="0666", SYMLINK="ndsadapter"

Upon plugging in the device, this will set it to be readable and writeable for all users on the system without restrictions. Aditionally it creates a symlink to the device as /dev/ndsadapter - but this is not required for the application to function (it will find the adapter regardless of what it is called, based on the USB vendor and product ID).

Using the program
=================

Simply invoking the ndsplus binary like `./ndsplus` will print status information to the screen, like so:

    INFO HERE

To backup a savegame, simply invoke the application like  `./ndsplus --backup <FILENAME HERE>`. For example `./ndsplus --backup somegame.sav`. This will attempt to read a backup of the savegame to the given filename. It will attempt to overwrite any existing file at that location without warning, so be careful.

To restore a savegame, just do `./ndsplus --restore <FILENAME HERE>` - this will work very much like the backup option, instead reading the existing file and writing it back to the card. There are absolutely no checks in place to see if you are writing a savegame meant for the inserted game. Writing a "wrong" savegame shouldn't do any harm to the card (it will simply wipe itself and start like it's new) but wiping the card is probably not what you intended.

Unless wiping the card is exactly what you intended - in that case just use `./ndsplus --wipe`. This will write all zeroes to the savegame area of the card, effectively wiping anything stored on it. Great for removing data from second-hand games or for when you intend to give away or sell your old games.

If you like seeing what is happening behind the scenes, use the `--debug` or `-d` option. It will print all the bytes going over the USB connection to standard error output. This can be used in combination with any combination of other options.

The --backup option has preference over the --restore option, so you can restore a save and create a backup of the current save at the same time by using both options simultaneously.

Firmware support
================

I wrote this program to mimic the official software that comes with firmware version 304. At time of writing, no newer firmware has been released yet and I didn't try this application with any of the older firmware versions, so I do not know how it will perform on them. If you want to be on the safe side, only use if your card is firmware version 304. I don't expect other versions to screw up anything, but who knows.

There is no support for writing firmware to the adapter at all - I might add this later if I see a reason/need to, possibly after the next firmware update.


Unsupported cards and incorrect information
===========================================

If you come across anything that isn't working where the official software does work, please post a log of the program's output and a description of the problem as a new issue on github. I'll see what I can do to fix it.
