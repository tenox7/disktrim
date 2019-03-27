DiskTrim for Windows
====================
Copyright (c) 2016 by Antoni Sawicki and Tomasz Nowak

Requires Windows 2012 R2 / Windows 8.1 or above

A small command line utility for Microsoft Windows that allows to
send TRIM / UNMAP / DISCARD commands directly to an SSD drive.  The 
operation is performed arbitrarily on a full sector range from zero
to the end. It securely erases contents of an entire SSD drive, and
tests whether TRIM actually worked. 

You can also think of it as equivalent of Linux `blkdiscard(8)` utility for Windows.

If you just want to test if your SSD supports TRIM without deleting
it's entire contents, you can simply create and mount a small .VHDX
file on top and run DiskTrim on the VHDX instead of physical disk. 

# WARNING:
This utility is very dangerous and if used incorrectly it
will ruin your life. Authors of this software application take absolutely no
responsibility for use of this program  and its consequences. 
