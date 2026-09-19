//
// DiskTrim 2.0 by Antoni Sawicki and Tomasz Nowak
// Requires Windows 2012 R2 / Windows 8.1 or above
//
// DiskTrim -- a small command line utility for Windows that allows to
// send ATA TRIM and SCSI UNMAP commands directly to an SSD drive. The
// operation is performed arbitrarily on a full sector range from zero
// to the end. It securely erases contents of an entire SSD drive, and
// tests whether TRIM actually worked.
//
// If you just want to test if your SSD supports TRIM without deleting
// it's entire contents, you can simply create and mount a small .VHDX
// file on top and run DiskTrim on the VHDX instead of physical disk.
//
// WARNING:
// This utility is particularly dangerous and if used incorrectly - it
// will permanently destroy contents of your SSD drive, and delete all
// your data.  Authors of this software application take absolutely no
// responsibility for use of this program  and its consequences.
//
#include <windows.h>
#include <ntddscsi.h>
#define _NTSCSI_USER_MODE_
#include <scsi.h>
#undef _NTSCSI_USER_MODE_
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdarg.h>

//#define SAFE

#define SENSE_INFO_LENGTH                   128
#define SCSI_TIMEOUT_SECONDS                5
#define UNMAP_TIMEOUT_SECONDS               600

//
// READ CAPACITY 10 carries a 32 bit LBA and saturates at 0xFFFFFFFF
// when the disk is too large to describe.
//
#define READ_CAPACITY10_SATURATED           0xFFFFFFFF

#define TEST_PATTERN                        L"====[Test*Pattern]===="

#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)
#define __WDATE__ WIDEN(__DATE__)
#define __WTIME__ WIDEN(__TIME__)

#define USAGE L"Usage: disktrim [-y] <disk #>\n\n"\
              L"Disk# number can be obtained from:\n"\
              L"- Disk Management (diskmgmt.msc)\n"\
              L"- diskpart (list disk)\n"\
              L"- wmic diskdrive get index,caption,size\n"\
              L"- get-disk\n"\
              L"- get-physicaldisk | ft deviceid,friendlyname\n\n"\
              L"Long form \\\\.\\PhysicalDriveXX is also allowed\n\n"

void error(int exit, WCHAR* msg, ...) {
    va_list valist;
    WCHAR vaBuff[1024] = { L'\0' };
    WCHAR errBuff[1024] = { L'\0' };
    DWORD err;

    err = GetLastError();

    va_start(valist, msg);
    vswprintf(vaBuff, ARRAYSIZE(vaBuff), msg, valist);
    va_end(valist);

    wprintf(L"%s: %s\n", (exit) ? L"ERROR" : L"WARNING", vaBuff);

    if (err) {
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errBuff, ARRAYSIZE(errBuff), NULL);
        wprintf(L"[0x%08X] %s\n\n", err, errBuff);
    }
    else {
        putchar(L'\n');
    }

    fflush(stdout);

    // Keep later messages from reporting this error as their own.
    SetLastError(0);

    if (exit)
        ExitProcess(1);
}

//
// DeviceIoControl returning TRUE only means the request reached the
// device.  The device can still reject the command with CHECK
// CONDITION in ScsiStatus and the reason in the sense data.
//
BOOL ScsiPassRejected(PSCSI_PASS_THROUGH pScsiPass, WCHAR* What) {
    PUCHAR              pSense;

    if (pScsiPass->ScsiStatus == 0)
        return FALSE;

    pSense = (PUCHAR)pScsiPass + pScsiPass->SenseInfoOffset;

    error(0, L"%s rejected by device: SCSI status 0x%02X, sense key 0x%02X, ASC 0x%02X, ASCQ 0x%02X",
        What, pScsiPass->ScsiStatus, pSense[2] & 0x0F, pSense[12], pSense[13]);

    return TRUE;
}

void ScsiPassInit(PSCSI_PASS_THROUGH pScsiPass, UCHAR CdbLength, UCHAR DataDirection, ULONG TransferSize) {
    pScsiPass->Length = sizeof(SCSI_PASS_THROUGH);
    pScsiPass->TargetId = 1;
    pScsiPass->PathId = 0;
    pScsiPass->Lun = 0;
    pScsiPass->CdbLength = CdbLength;
    pScsiPass->SenseInfoLength = SENSE_INFO_LENGTH;
    pScsiPass->SenseInfoOffset = sizeof(SCSI_PASS_THROUGH);
    pScsiPass->DataIn = DataDirection;
    pScsiPass->TimeOutValue = SCSI_TIMEOUT_SECONDS;
    pScsiPass->DataTransferLength = TransferSize;
    pScsiPass->DataBufferOffset = pScsiPass->SenseInfoOffset + pScsiPass->SenseInfoLength;
}

//
// Ask the device for its last LBA and block size.  Returns FALSE if
// the request fails or the device rejects the command, so the caller
// can fall back to another source.
//
BOOL ScsiReadCapacity(HANDLE hDisk, BOOL Use16, ULONG64* LastLba, ULONG* BlockSize) {
    PSCSI_PASS_THROUGH  pScsiPass;
    PCDB                pCdb;
    PREAD_CAPACITY16_DATA pReply16;
    PREAD_CAPACITY_DATA pReply10;
    ULONG               ReplyLen;
    ULONG               BufLen;
    ULONG               BytesRet;
    ULONG               LastLba10;
    BOOL                Ok;

    ReplyLen = (Use16) ? sizeof(READ_CAPACITY16_DATA) : sizeof(READ_CAPACITY_DATA);
    BufLen = sizeof(SCSI_PASS_THROUGH) + SENSE_INFO_LENGTH + ReplyLen;

    pScsiPass = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BufLen);

    if (pScsiPass == NULL)
        error(1, L"Cannot allocate %lu bytes for READ CAPACITY", BufLen);

    ScsiPassInit(pScsiPass, (Use16) ? 16 : 10, SCSI_IOCTL_DATA_IN, ReplyLen);

    pCdb = (PCDB)pScsiPass->Cdb;

    if (Use16) {
        pCdb->READ_CAPACITY16.OperationCode = SCSIOP_READ_CAPACITY16;
        pCdb->READ_CAPACITY16.ServiceAction = SERVICE_ACTION_READ_CAPACITY16;
        REVERSE_BYTES(pCdb->READ_CAPACITY16.AllocationLength, &ReplyLen);
    }
    else {
        pCdb->CDB10.OperationCode = SCSIOP_READ_CAPACITY;
    }

    // A rejected command still succeeds here, with the reason in ScsiStatus.
    Ok = DeviceIoControl(hDisk, IOCTL_SCSI_PASS_THROUGH, pScsiPass, BufLen, pScsiPass, BufLen, &BytesRet, NULL)
        && pScsiPass->ScsiStatus == 0;

    if (Ok && Use16) {
        pReply16 = (PREAD_CAPACITY16_DATA)((PUCHAR)pScsiPass + pScsiPass->DataBufferOffset);

        REVERSE_BYTES_QUAD(LastLba, &pReply16->LogicalBlockAddress);
        REVERSE_BYTES(BlockSize, &pReply16->BytesPerBlock);
    }
    else if (Ok) {
        pReply10 = (PREAD_CAPACITY_DATA)((PUCHAR)pScsiPass + pScsiPass->DataBufferOffset);

        REVERSE_BYTES(&LastLba10, &pReply10->LogicalBlockAddress);
        REVERSE_BYTES(BlockSize, &pReply10->BytesPerBlock);
        *LastLba = LastLba10;
    }

    HeapFree(GetProcessHeap(), 0, pScsiPass);

    return Ok;
}

int wmain(int argc, WCHAR* argv[]) {
    HANDLE              hDisk;
    WCHAR               DevName[64] = { '\0' };
    OVERLAPPED          Ovr = { 0 };
    WCHAR               TestBuff[512] = { '\0' };
    WCHAR*              DiskNo = NULL;
    DWORD               y = 0;
    wint_t              p;
    PSCSI_PASS_THROUGH  pScsiPass;
    GET_LENGTH_INFORMATION  DiskLengthInfo;
    DISK_GEOMETRY       DiskGeometry = { 0 };
    ULONG               WinSectorSize;
    ULONG64             WinLbaTotal;
    WCHAR*              CapacitySource;
    STORAGE_PROPERTY_QUERY trim_q = { StorageDeviceTrimProperty,  PropertyStandardQuery };
    DEVICE_TRIM_DESCRIPTOR trim_d = { 0 };
    BOOL                TrimQueryOk;
    STORAGE_PROPERTY_QUERY desc_q = { StorageDeviceProperty,  PropertyStandardQuery };
    STORAGE_DESCRIPTOR_HEADER desc_h = { 0 };
    PSTORAGE_DEVICE_DESCRIPTOR desc_d;
    PVOID               Buffer;
    ULONG               BufLen;
    ULONG               TransferSize;
    PCDB                pCdb;
    PUNMAP_LIST_HEADER  pUnmapHdr;
    ULONG               BytesRet;
    PUCHAR              pSenseCode;
    ULONG               DiskBlockSize;
    ULONG64             DiskLbaCount;
    ULONG               UnmapEntryCount;
    ULONG               i;
    ULONG64             LbaStart, LbaCount;
    USHORT              TransferSizeAsUShort;
    ULONG               LbaCountAsULong;


    wprintf(L"DiskTrim v2.2 by Antoni Sawicki & Tomasz Nowak, Build %s %s\n\n", __WDATE__, __WTIME__);

    if (argc == 3) {
        if (wcscmp(argv[1], L"-y") == 0) {
            DiskNo = argv[2];
            y = 1;
        }
        else {
            error(1, L"argc=3 argv[1]=%s argv[2]=%s\n\n%s\n", argv[1], argv[2], USAGE);
        }
    }
    else if (argc == 2) {
        DiskNo = argv[1];
    }
    else {
        error(1, L"Wrong number of parameters [argc=%d]\n\n%s\n", argc, USAGE);
    }

    if (DiskNo == NULL)
        error(1, L"DiskNo is empty\n");

    if (wcsnicmp(DiskNo, L"\\\\.\\PhysicalDrive", 17) == 0)
        wcsncpy(DevName, DiskNo, ARRAYSIZE(DevName));
    else if (iswdigit(*DiskNo))
        swprintf(DevName, ARRAYSIZE(DevName), L"\\\\.\\PhysicalDrive%s", DiskNo);
    else
        error(1, USAGE, argv[0]);

    if ((hDisk = CreateFileW(DevName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL)) == INVALID_HANDLE_VALUE)
        error(1, L"Cannot open %s", DevName);

    if (!DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &DiskLengthInfo, sizeof(GET_LENGTH_INFORMATION), &BytesRet, NULL))
        error(1, L"Error on DeviceIoControl IOCTL_DISK_GET_LENGTH_INFO [%d] ", BytesRet);

    //
    // The TRIM property only feeds the "Trim:" console output.  Many
    // USB bridges do not implement it, and whether UNMAP works is
    // settled by the UNMAP command itself.
    //
    TrimQueryOk = DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &trim_q, sizeof(trim_q), &trim_d, sizeof(trim_d), &BytesRet, NULL);

    if (!TrimQueryOk)
        error(0, L"DeviceIoControl IOCTL_STORAGE_QUERY_PROPERTY Trim Property not answered by this device, continuing");

    if (!DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &desc_q, sizeof(desc_q), &desc_h, sizeof(desc_h), &BytesRet, NULL))
        error(1, L"Error on DeviceIoControl IOCTL_STORAGE_QUERY_PROPERTY Device Property [%d] ", BytesRet);

    desc_d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, desc_h.Size);

    if (!DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &desc_q, sizeof(desc_q), desc_d, desc_h.Size, &BytesRet, NULL))
        error(1, L"Error on DeviceIoControl IOCTL_STORAGE_QUERY_PROPERTY [%d] ", BytesRet);

    wprintf(L"Disk: %s\nSize: %.1f GB \n", DiskNo, (float)DiskLengthInfo.Length.QuadPart / 1024.0 / 1024.0 / 1024.0);

    if (desc_d->Version == sizeof(STORAGE_DEVICE_DESCRIPTOR))
        wprintf(L"Type: %S %S\n",
            (desc_d->VendorIdOffset) ? (char*)desc_d + desc_d->VendorIdOffset : "n/a",
            (desc_d + desc_d->ProductIdOffset) ? (char*)desc_d + desc_d->ProductIdOffset : "n/a"
        );

    if (!TrimQueryOk)
        wprintf(L"Trim: Unknown (query failed)\n");
    else if (trim_d.Version == sizeof(DEVICE_TRIM_DESCRIPTOR) && trim_d.TrimEnabled == 1)
        wprintf(L"Trim: Supported\n");
    else
        wprintf(L"Trim: Not Supported\n");


    if (!y) {
        wprintf(L"\n"
            L"WARNING: Contents of your drive an all data will be permanently erased! \n"
            L"There is no possibility of data recovery even with 3rd party companies.\n\n"
            L"Do you want to erase this disk (y/N) ? ");
        p = getwchar();
        if (p == L'y')
            wprintf(L"All right...\n");
        else
            error(1, L"\rAborting...\n");
    }

    //
    // Query disk size
    //
    wprintf(L"Querying drive parameters...\n");

    if (!DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &DiskGeometry, sizeof(DiskGeometry), &BytesRet, NULL))
        error(1, L"Error on DeviceIoControl IOCTL_DISK_GET_DRIVE_GEOMETRY [%d] ", BytesRet);

    WinSectorSize = DiskGeometry.BytesPerSector;
    WinLbaTotal = (ULONG64)DiskLengthInfo.Length.QuadPart / WinSectorSize;

    //
    // UNMAP addresses the device's LBA space, so prefer the device's
    // own answer.  Fall back to READ CAPACITY 10 when READ CAPACITY 16
    // is rejected, and to Windows' geometry when neither answers or
    // READ CAPACITY 10 is saturated.
    //
    if (ScsiReadCapacity(hDisk, TRUE, &DiskLbaCount, &DiskBlockSize)) {
        CapacitySource = L"READ CAPACITY 16";
    }
    else if (ScsiReadCapacity(hDisk, FALSE, &DiskLbaCount, &DiskBlockSize)
        && DiskLbaCount != READ_CAPACITY10_SATURATED) {
        CapacitySource = L"READ CAPACITY 10";
    }
    else {
        if (WinLbaTotal == 0)
            error(1, L"Neither the device nor Windows reports a usable capacity");

        DiskLbaCount = WinLbaTotal - 1;
        DiskBlockSize = WinSectorSize;
        CapacitySource = L"Windows geometry";
    }

    if (DiskBlockSize == 0 || DiskLbaCount == 0)
        error(1, L"Unusable capacity from %s: last LBA %I64u, block %lu bytes", CapacitySource, DiskLbaCount, DiskBlockSize);

    if (DiskLbaCount + 1 != WinLbaTotal || DiskBlockSize != WinSectorSize)
        error(0, L"Capacity disagreement: device says %I64u blocks of %lu bytes, Windows says %I64u of %lu",
            DiskLbaCount + 1, DiskBlockSize, WinLbaTotal, WinSectorSize);

    wprintf(L"%s LBA: %I64u, Block: %lu, Size: %.1f GB [via %s]\n", DevName, DiskLbaCount, DiskBlockSize,
        (float)(((float)(DiskLbaCount + 1) * (float)DiskBlockSize) / 1024.0 / 1024.0 / 1024.0), CapacitySource);

    // There is no going back after this...
#ifdef SAFE
    return 0;
#endif

    //
    // Uninitialize disk so it doesn't have any partitions in order for pass through to work
    //
    wprintf(L"Deleting disk partitions...\n");
    if (!DeviceIoControl(hDisk, IOCTL_DISK_DELETE_DRIVE_LAYOUT, NULL, 0, NULL, 0, &BytesRet, NULL))
        error(1, L"Error on DeviceIoControl IOCTL_DISK_DELETE_DRIVE_LAYOUT [%d] ", BytesRet);

    //
    // Write test pattern
    //
    wprintf(L"Writing test pattern...\n");
    ZeroMemory(&Ovr, sizeof(Ovr));
    Ovr.Offset = 0x00;
    Ovr.OffsetHigh = 0;

    ZeroMemory(TestBuff, sizeof(TestBuff));
    swprintf(TestBuff, ARRAYSIZE(TestBuff), TEST_PATTERN);

    if (!WriteFile(hDisk, TestBuff, sizeof(TestBuff), NULL, &Ovr))
        error(1, L"Error writing test pattern to disk");

    ZeroMemory(TestBuff, sizeof(TestBuff));

    if (!ReadFile(hDisk, TestBuff, sizeof(TestBuff), NULL, &Ovr))
        error(1, L"Error reading disk");

    wprintf(L"Buffer before TRIM: \"%s\"\n", TestBuff);

    if (wcscmp(TestBuff, TEST_PATTERN) != 0)
        error(1, L"Unable to write test pattern to disk");

    //
    // UNMAP
    //
    wprintf(L"Performing UNMAP on the LBA range...\n");

    UnmapEntryCount = (DiskLbaCount >> 32) + 1;

    TransferSize = sizeof(UNMAP_LIST_HEADER) + (UnmapEntryCount * sizeof(UNMAP_BLOCK_DESCRIPTOR));

    BufLen = sizeof(SCSI_PASS_THROUGH) + SENSE_INFO_LENGTH + TransferSize;

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BufLen);

    (PVOID)pScsiPass = Buffer;

    ScsiPassInit(pScsiPass, 10, SCSI_IOCTL_DATA_OUT, TransferSize);

    // Discarding every LBA on the device can take far longer than a query.
    pScsiPass->TimeOutValue = UNMAP_TIMEOUT_SECONDS;

    pSenseCode = (PUCHAR)Buffer + pScsiPass->SenseInfoOffset;

    (PVOID)pCdb = pScsiPass->Cdb;
    pCdb->UNMAP.OperationCode = SCSIOP_UNMAP;
    pCdb->UNMAP.Anchor = 0;
    pCdb->UNMAP.GroupNumber = 0;
    TransferSizeAsUShort = (USHORT)TransferSize;
    REVERSE_BYTES_SHORT(pCdb->UNMAP.AllocationLength, &TransferSizeAsUShort);

    (PVOID)pUnmapHdr = (PUCHAR)pScsiPass + pScsiPass->DataBufferOffset;

    TransferSizeAsUShort = (USHORT)(TransferSize - 2);
    REVERSE_BYTES_SHORT(pUnmapHdr->DataLength, &TransferSizeAsUShort);
    TransferSizeAsUShort = (USHORT)(TransferSize - sizeof(UNMAP_LIST_HEADER));
    REVERSE_BYTES_SHORT(pUnmapHdr->BlockDescrDataLength, &TransferSizeAsUShort);


    LbaStart = 0;
    LbaCount = DiskLbaCount + 1;

    for (i = 0; i < UnmapEntryCount; i++) {
        LbaCountAsULong = (ULONG)((LbaCount < 0xFFFFFFFF) ? (LbaCount & 0xFFFFFFFF) : 0xFFFFFFFF);
        REVERSE_BYTES_QUAD(pUnmapHdr->Descriptors[i].StartingLba, &LbaStart);
        REVERSE_BYTES(pUnmapHdr->Descriptors[i].LbaCount, &LbaCountAsULong);

        if (LbaCount > 0xFFFFFFFF) {
            LbaCount -= 0xFFFFFFFF;
            LbaStart += 0xFFFFFFFF;
        }
    }


    if (!DeviceIoControl(hDisk, IOCTL_SCSI_PASS_THROUGH, Buffer, BufLen, Buffer, BufLen, &BytesRet, NULL))
        error(1, L"Error performing DeviceIoControl IOCTL_SCSI_PASS_THROUGH");

    if (ScsiPassRejected(pScsiPass, L"UNMAP"))
        error(1, L"The device refused the UNMAP command, nothing was discarded");

    ZeroMemory(TestBuff, sizeof(TestBuff));

    wprintf(L"Reading test pattern...\n");
    if (!ReadFile(hDisk, TestBuff, sizeof(TestBuff), NULL, &Ovr))
        error(1, L"Error reading disk");

    wprintf(L"Buffer after TRIM : \"%s\" [if empty, TRIM worked]\n", TestBuff);

    if (wcscmp(TestBuff, TEST_PATTERN) == 0)
        error(1, L"TRIM didn't seem to work\n");

    wprintf(L"Looks like TRIM worked!\n");

    fflush(stdout);

    return 0;
}
