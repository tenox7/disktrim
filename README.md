DiskTrim for Windows
====================
Utility for Microsoft Windows that allows you to send TRIM / UNMAP / DISCARD commands directly to an SSD drive. The operation is performed on the whole drive, sectors zero to the end. It securely erases the contents of an entire SSD drive, and tests whether TRIM actually worked. You can also think of it as the Windows equivalent of the Linux `blkdiscard(8)` utility.

# WARNING:
This utility is very dangerous and will irreversibly destroy all your data.
Once the operation is performed, the contents of the SSD are unrecoverable in any way.
The authors of this software application take absolutely no
responsibility for use of this program and its consequences.

## Legal
Copyright (c) 2016 by Antoni Sawicki and Tomasz Nowak

License: Apache 2.0
