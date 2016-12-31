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
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdarg.h>

#pragma pack(1)

typedef struct _CDB_10 {
    UCHAR       OperationCode;    // 0x42 - SCSIOP_UNMAP
    UCHAR       Anchor : 1;
    UCHAR       Reserved1 : 7;
    UCHAR       Reserved2[4];
    UCHAR       GroupNumber : 5;
    UCHAR       Reserved3 : 3;
    UCHAR       AllocationLength[2];
    UCHAR       Control;
} CDB_10, *PCDB_10;

typedef struct _CDB_16 {
    UCHAR       OperationCode;
    UCHAR       ServiceAction : 5;
    UCHAR       Reserved1 : 3;
    UCHAR       LBA[8];
    UCHAR       AllocationLength[4];
    UCHAR       PMI : 1;
    UCHAR       Reserved2 : 7;
    UCHAR       Control;
} CDB_16, *PCDB_16;


typedef struct _UNMAP_BLOCK_DESCRIPTOR {
    ULONG64 StartingLba;
    ULONG LbaCount;
    UCHAR Reserved[4];
} UNMAP_BLOCK_DESCRIPTOR, *PUNMAP_BLOCK_DESCRIPTOR;

typedef struct _UNMAP_LIST_HEADER {
    USHORT DataLength;
    USHORT BlockDescrDataLength;
    UCHAR Reserved[4];
    UNMAP_BLOCK_DESCRIPTOR Descriptors[0];
} UNMAP_LIST_HEADER, *PUNMAP_LIST_HEADER;


typedef struct _READ_CAPACITY10 {
    ULONG       LBA;
    ULONG       BlockLength;
} READ_CAPACITY10, *PREAD_CAPACITY10;

typedef struct _READ_CAPACITY16 {
    ULONG64     LBA;
    ULONG       BlockLength;
    UCHAR       ProtEn : 1;
    UCHAR       PType : 3;
    UCHAR       Reserved1 : 4;
    UCHAR       LBPerPBExp : 4;
    UCHAR       PIExp : 4;
    UCHAR       MSB : 6;
    UCHAR       TPRZ : 1;
    UCHAR       TPE : 1;
    UCHAR       LSB;
    UCHAR       Reserved2[16];

} READ_CAPACITY16, *PREAD_CAPACITY16;

#pragma pack()


typedef struct _SCSI_PASS_THROUGH {
    USHORT    Length;
    UCHAR     ScsiStatus;
    UCHAR     PathId;
    UCHAR     TargetId;
    UCHAR     Lun;
    UCHAR     CdbLength;
    UCHAR     SenseInfoLength;
    UCHAR     DataIn;
    ULONG     DataTransferLength;
    ULONG     TimeOutValue;
    ULONG_PTR DataBufferOffset;
    ULONG     SenseInfoOffset;
    UCHAR     Cdb[16];
} SCSI_PASS_THROUGH, *PSCSI_PASS_THROUGH;

#define REVERSE_BYTES_SHORT( x ) ( ((x & 0xFF) << 8) | ((x & 0xFF00) >> 8))
#define REVERSE_BYTES_LONG( x ) ( ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x & 0xFF0000) >> 8) | ((x & 0xFF000000) >> 24))
#define REVERSE_BYTES_LONG64( x ) ( ((x & 0xFF) << 56) | ((x & 0xFF00) << 40) | ((x & 0xFF0000) << 24) | ((x & 0xFF000000) << 8) | ((x & 0xFF00000000) >> 8) | ((x & 0xFF0000000000) >> 24) | ((x & 0xFF000000000000) >> 40) | ((x & 0xFF00000000000000) >> 56) )

#define SRB_FLAGS_DATA_IN                   0x00000040
#define SRB_FLAGS_DATA_OUT                  0x00000080

#define IOCTL_SCSI_BASE                     0x00000004
#define IOCTL_SCSI_PASS_THROUGH             CTL_CODE(IOCTL_SCSI_BASE, 0x0401, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define SENSE_INFO_LENGTH                   128

#define TEST_PATTERN                        L"====[Test*Pattern]===="

#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)
#define __WDATE__ WIDEN(__DATE__)
#define __WTIME__ WIDEN(__TIME__)

#define USAGE L"Usage: disktrim [-y] <disk #>\n\n"\
              L"Disk# number can be obtained from:\n"\
              L"- diskmgmt.msc\n"\
              L"- diskpart (list disk)\n"\
              L"- wmic diskdrive get index,caption,size\n"\
              L"- get-disk\n"\
              L"- get-physicaldisk | ft deviceid,friendlyname\n\n"\
              L"Long form \\\\.\\PhysicalDriveXX is also allowed\n\n"

void error(int exit, WCHAR *msg, ...) {
    va_list valist;
    WCHAR vaBuff[1024]={L'\0'};
    WCHAR errBuff[1024]={L'\0'};
    DWORD err;

    err=GetLastError();

    va_start(valist, msg);
    _vsnwprintf_s(vaBuff, sizeof(vaBuff), sizeof(vaBuff), msg, valist);
    va_end(valist);

    wprintf(L"%s: %s\n", (exit) ? L"ERROR":L"WARNING", vaBuff);

    if (err) {
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errBuff, sizeof(errBuff), NULL);
        wprintf(L"[0x%08X] %s\n\n", err, errBuff);
    }
    else {
        putchar(L'\n');
    }

    FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));

    if(exit)
        ExitProcess(1);
}

int wmain(int argc, WCHAR *argv[]) {
    HANDLE              hDisk;
    WCHAR               DevName[64] = { '\0' };
    OVERLAPPED          Ovr = { 0 };
    WCHAR               TestBuff[512] = { '\0' };
    WCHAR               *DiskNo;
    DWORD               y = 0;
    wint_t              p;
    PSCSI_PASS_THROUGH  ScsiPass;
    GET_LENGTH_INFORMATION  DiskLengthInfo;
    PVOID               Buffer;
    ULONG               BufLen;
    ULONG               TransferSize;
    PCDB_10             pCDB;
    PCDB_16             pCDB16;
    PUNMAP_LIST_HEADER  pUnmapHdr;
    ULONG               BytesRet;
    PUCHAR              pSenseCode;
    PREAD_CAPACITY16    pReadCapacity;
    ULONG               DiskBlockSize;
    ULONG64             DiskLbaCount;
    ULONG               UnmapEntryCount;
    ULONG               i;
    ULONG64             LBAStart, LBACount;


    wprintf(L"DiskTrim v2.0 by Antoni Sawicki & Tomasz Nowak, Build %s %s\n\n", __WDATE__, __WTIME__);

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

    if (_wcsnicmp(DiskNo, L"\\\\.\\PhysicalDrive", wcslen(L"\\\\.\\PhysicalDrive")) == 0)
        wcsncpy_s(DevName, sizeof(DevName), DiskNo, sizeof(DevName));
    else if (iswdigit(DiskNo[0]))
        _snwprintf_s(DevName, sizeof(DevName) / sizeof(WCHAR), sizeof(DevName), L"\\\\.\\PhysicalDrive%s", DiskNo);
    else
        error(1, USAGE, argv[0]);

    if ((hDisk = CreateFileW(DevName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL)) == INVALID_HANDLE_VALUE)
        error(1, L"Cannot open %s", DevName);

    if (!DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &DiskLengthInfo, sizeof(GET_LENGTH_INFORMATION), &BytesRet, NULL))
        error(1, L"Error on DeviceIoControl IOCTL_DISK_GET_LENGTH_INFO [%d] ", BytesRet);


    if (!y) {
        wprintf(L"WARNING: Contents of your drive an all data will be permanently erased! \n");
        wprintf(L"\nDo you want to erase disk %s, Size: %.1f GB, (y/N) ? ", DiskNo, (float)DiskLengthInfo.Length.QuadPart / 1024.0 / 1024.0 / 1024.0);
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

    TransferSize = 36;

    BufLen = sizeof(SCSI_PASS_THROUGH) + SENSE_INFO_LENGTH + TransferSize;

    Buffer = malloc(BufLen);
    ZeroMemory(Buffer, BufLen);

    (PVOID)ScsiPass = Buffer;

    ScsiPass->Length = sizeof(SCSI_PASS_THROUGH);
    ScsiPass->TargetId = 1;
    ScsiPass->PathId = 0;
    ScsiPass->Lun = 0;
    ScsiPass->CdbLength = 16;
    ScsiPass->SenseInfoLength = SENSE_INFO_LENGTH;
    ScsiPass->SenseInfoOffset = sizeof(SCSI_PASS_THROUGH);
    ScsiPass->DataIn = SRB_FLAGS_DATA_IN;
    ScsiPass->TimeOutValue = 5000;
    ScsiPass->DataTransferLength = TransferSize;
    ScsiPass->DataBufferOffset = ScsiPass->SenseInfoOffset + ScsiPass->SenseInfoLength;

    pSenseCode = (PUCHAR)Buffer + ScsiPass->SenseInfoOffset;

    (PVOID)pCDB16 = ScsiPass->Cdb;
    pCDB16->OperationCode = 0x9E;
    pCDB16->ServiceAction = 0x10;

    (PVOID)pReadCapacity = (PUCHAR)Buffer + ScsiPass->DataBufferOffset;


    if (!DeviceIoControl(hDisk, IOCTL_SCSI_PASS_THROUGH, Buffer, BufLen, Buffer, BufLen, &BytesRet, NULL))
        error(1, L"Error on DeviceIoControl IOCTL_SCSI_PASS_THROUGH");

    DiskLbaCount = REVERSE_BYTES_LONG64(pReadCapacity->LBA);
    DiskBlockSize = REVERSE_BYTES_LONG(pReadCapacity->BlockLength);

    wprintf(L"%s LBA: %I64u, Block: %lu, Size: %.1f GB\n", DevName, DiskLbaCount, DiskBlockSize, (float)(((float)DiskLbaCount*(float)DiskBlockSize) / 1024.0 / 1024.0 / 1024.0));

    free(Buffer);

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
    _snwprintf_s(TestBuff, sizeof(TestBuff) / sizeof(WCHAR), sizeof(TestBuff), TEST_PATTERN);

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

    Buffer = malloc(BufLen);
    ZeroMemory(Buffer, BufLen);

    (PVOID)ScsiPass = Buffer;

    ScsiPass->Length = sizeof(SCSI_PASS_THROUGH);
    ScsiPass->TargetId = 1;
    ScsiPass->PathId = 0;
    ScsiPass->Lun = 0;
    ScsiPass->CdbLength = 10;
    ScsiPass->SenseInfoLength = SENSE_INFO_LENGTH;
    ScsiPass->SenseInfoOffset = sizeof(SCSI_PASS_THROUGH);
    ScsiPass->DataIn = SRB_FLAGS_DATA_OUT;
    ScsiPass->TimeOutValue = 5000;
    ScsiPass->DataTransferLength = TransferSize;
    ScsiPass->DataBufferOffset = sizeof(SCSI_PASS_THROUGH) + ScsiPass->SenseInfoLength;

    pSenseCode = (PUCHAR)Buffer + ScsiPass->SenseInfoOffset;

    (PVOID)pCDB = ScsiPass->Cdb;
    pCDB->OperationCode = 0x42;
    pCDB->Anchor = 0;
    pCDB->GroupNumber = 0;
    pCDB->AllocationLength[0] = (UCHAR)(TransferSize >> 8);
    pCDB->AllocationLength[1] = (UCHAR)TransferSize;

    (PVOID)pUnmapHdr = (PUCHAR)ScsiPass + ScsiPass->DataBufferOffset;

    pUnmapHdr->DataLength = REVERSE_BYTES_SHORT(TransferSize - 2);
    pUnmapHdr->BlockDescrDataLength = REVERSE_BYTES_SHORT(TransferSize - sizeof(UNMAP_LIST_HEADER));


    LBAStart = 0;
    LBACount = DiskLbaCount + 1;

    for (i = 0; i < UnmapEntryCount; i++) {
        pUnmapHdr->Descriptors[i].StartingLba = REVERSE_BYTES_LONG64(LBAStart);
        pUnmapHdr->Descriptors[i].LbaCount = REVERSE_BYTES_LONG((ULONG)((LBACount < 0xFFFFFFFF) ? (LBACount & 0xFFFFFFFF) : 0xFFFFFFFF));

        if (LBACount > 0xFFFFFFFF) {
            LBACount -= 0xFFFFFFFF;
            LBAStart += 0xFFFFFFFF;
        }
    }


    if (!DeviceIoControl(hDisk, IOCTL_SCSI_PASS_THROUGH, Buffer, BufLen, Buffer, BufLen, &BytesRet, NULL))
        error(1, L"Error performing DeviceIoControl IOCTL_SCSI_PASS_THROUGH");

    ZeroMemory(TestBuff, sizeof(TestBuff));

    wprintf(L"Reading test pattern...\n");
    if (!ReadFile(hDisk, TestBuff, sizeof(TestBuff), NULL, &Ovr))
        error(1, L"Error reading disk");

    wprintf(L"Buffer after TRIM : \"%s\" [if empty, TRIM worked]\n", TestBuff);

    if (wcscmp(TestBuff, TEST_PATTERN) == 0)
        error(1, L"TRIM didn't seem to work\n");

    wprintf(L"Looks like TRIM worked!\n");

    FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));

    return 0;
}
