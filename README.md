DiskTrim for Windows
====================
Utility for Microsoft Windows that allows to send TRIM / UNMAP / DISCARD commands directly to an SSD drive.  The operation is performed on the whole drive, sectors zero to the end. It securely erases contents of an entire SSD drive, and tests whether TRIM actually worked. You can also think of it as equivalent of Linux `blkdiscard(8)` utility for Windows.

# WARNING:
This utility is very dangerous and will irreversibly destroy all your data.
Once the operation is performed the old data on your SSD drive is unrecoverable in any way. 
Authors of this software application take absolutely no
responsibility for use of this program  and its consequences. 

## Legal
Copyright (c) 2016 by Antoni Sawicki and Tomasz Nowak

License: Apache 2.0
