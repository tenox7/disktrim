//
// DiskTrim 1.1 by Antoni Sawicki and Tomasz Nowak
// Requires Windows 2012 R2 / Windows 8.1 or above
//
// DiskTrim is a small command line application for Windows that allows
// to send  ATA TRIM / SCSI UNMAP command directly to an SSD using SCSI
// pass through. It functionis to securely erase contents of an SSD and
// test whether TRIM actually worked.  If you just want to test if your
// SSD supports TRIM under Windows without deleting it's contents,  you
// can create and mount a small VHDX file and run DiskTrim on the  VHDX
// instead of physical disk. 
//
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdarg.h>

typedef struct _CDB_UNMAP {
    UCHAR OperationCode;    // 0x42 - SCSIOP_UNMAP
    UCHAR Anchor        : 1;
    UCHAR Reserved1     : 7;
    UCHAR Reserved2[4];
    UCHAR GroupNumber   : 5;
    UCHAR Reserved3     : 3;
    UCHAR AllocationLength[2];
    UCHAR Control;
} CDB_UNMAP, *PCDB_UNMAP;


#pragma pack(1)

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


typedef struct _READ_CAPACITY {
    ULONG       LBA;
    ULONG       BlockLength;
} READ_CAPACITY, *PREAD_CAPACITY;

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

#define REVERSE_BYTES_SHORT( x ) ( ((x & 0xFF) << 8) | (x & 0xFF00) >> 8)
#define REVERSE_BYTES_LONG( x ) ( ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x & 0xFF0000) >> 8) | (x & 0xFF000000) >> 24)

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

#define USAGE L"Usage: %s [-y] <disk #>\n\nDisk# number can be obtained from:\n"\
              L"- diskmgmt.msc\n"\
              L"- diskpart (list disk)\n"\
              L"- get-disk\n"\
              L"- get-physicaldisk | ft deviceid,friendlyname\n\n"

void error(int exit, WCHAR *msg, ...) {
    va_list ap, valist;
    WCHAR vaBuff[1024]={'\0'};
    WCHAR errBuff[1024]={'\0'};
    DWORD err;

    va_start(valist, msg);
    _vsnwprintf(vaBuff, sizeof(vaBuff), msg, valist);
    va_end(valist);

    wprintf(L"ERROR: %s\n", vaBuff);
    err=GetLastError();
    if(err) {
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errBuff, sizeof(errBuff) , NULL );
        wprintf(L"[%08X] %s\n\n", err, errBuff);
    } 
    else {
        putchar(L'\n');
    }
    
    if(exit)
        ExitProcess(1);
}

int wmain(int argc, WCHAR *argv[]) {
    HANDLE              hDisk;
    WCHAR               DevName[64]={'\0'};
    OVERLAPPED          Ovr={0};
    WCHAR               TestBuff[512]={'\0'};
    WCHAR               *DiskNo;
    ULONG               i;
    DWORD               y=0;
    wint_t              p;
    PSCSI_PASS_THROUGH  ScsiPass;
    GET_LENGTH_INFORMATION  DiskLengthInfo;
    PVOID               Buffer;
    ULONG               BufLen;
    ULONG               TransferSize;
    PCDB_UNMAP          pCDB;
    PUNMAP_LIST_HEADER  pUnmapHdr;
    ULONG               BytesRet;
    PUCHAR              pSenseCode;
    PREAD_CAPACITY      pReadCapacity;
    ULONG               DiskLbaCount, DiskBlockSize;

    wprintf(L"=[ DiskTrim v1.1 by Antoni Sawicki & Tomasz Nowak, %s %s ]=\n\n", __WDATE__, __WTIME__);


    if(argc==3) {
        if(wcscmp(argv[1], L"-y")==0) {
            DiskNo=argv[2];
            y=1;
        }
        else {
            error(1, USAGE, argv[0]);
        }
    }
    else if(argc==2) {
        DiskNo=argv[1];
    }
    else {
        error(1, USAGE, argv[0]);
    }

    _snwprintf(DevName, sizeof(DevName), L"\\\\.\\PhysicalDrive%s", DiskNo);

    if ((hDisk = CreateFileW(DevName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL )) == INVALID_HANDLE_VALUE)
        error(1, L"Cannot open %s", DevName);

    if(!DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &DiskLengthInfo, sizeof(GET_LENGTH_INFORMATION), &BytesRet, NULL)) 
        error(1, L"Error on DeviceIoControl IOCTL_DISK_GET_LENGTH_INFO [%d] ", BytesRet );


    if(!y) {
        wprintf(L"WARNING: Contents of your drive an all data will be permanently erased! \n");
        wprintf(L"\nDo you want to erase disk %s, Size: %.1f GB, (y/N) ? ", DiskNo, (float)DiskLengthInfo.Length.QuadPart/1024.0/1024.0/1024.0);
        p=getwchar();
        if(p==L'y')
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
    ScsiPass->CdbLength = 12;
    ScsiPass->SenseInfoLength = SENSE_INFO_LENGTH;
    ScsiPass->SenseInfoOffset = sizeof(SCSI_PASS_THROUGH);
    ScsiPass->DataIn = SRB_FLAGS_DATA_IN;
    ScsiPass->TimeOutValue = 5000;
    ScsiPass->DataTransferLength = TransferSize;
    ScsiPass->DataBufferOffset = ScsiPass->SenseInfoOffset + ScsiPass->SenseInfoLength;

    pSenseCode = (PUCHAR)Buffer + ScsiPass->SenseInfoOffset;

    (PVOID)pReadCapacity = (PUCHAR)Buffer + ScsiPass->DataBufferOffset;

    (PVOID)pCDB = ScsiPass->Cdb;
    pCDB->OperationCode = 0x25;
    pCDB->Anchor = 0;
    pCDB->GroupNumber = 0;
    pCDB->AllocationLength[0] = 0;
    pCDB->AllocationLength[1] = 0;

    if(!DeviceIoControl(hDisk, IOCTL_SCSI_PASS_THROUGH, Buffer, BufLen, Buffer, BufLen, &BytesRet, NULL))
        error(1, L"Error on DeviceIoControl IOCTL_SCSI_PASS_THROUGH");

    DiskLbaCount = REVERSE_BYTES_LONG(pReadCapacity->LBA);
    DiskBlockSize = REVERSE_BYTES_LONG(pReadCapacity->BlockLength);

    /*wprintf(L"SCSI Status: %u\n", ScsiPass->ScsiStatus);
    wprintf(L"Sense Code: ");
    for (i = 0; i<32; i++)
        wprintf(L"%02X ", pSenseCode[i]);
    wprintf(L"\n");*/
    wprintf(L"%s LBA: %lu, Block: %lu, Size: %.1f GB\n", DevName, DiskLbaCount, DiskBlockSize, (float)(((float)DiskLbaCount*(float)DiskBlockSize)/1024.0/1024.0/1024.0) );

    free(Buffer);

    //
    // Uninitialize disk so it doesn't have any partitions
    //
    wprintf(L"Deleting disk partitions...\n");
    if(!DeviceIoControl(hDisk, IOCTL_DISK_DELETE_DRIVE_LAYOUT, NULL, 0, NULL, 0, &BytesRet, NULL)) 
       error(1, L"Error on DeviceIoControl IOCTL_DISK_DELETE_DRIVE_LAYOUT [%d] ", BytesRet );


    //
    // Write test pattern
    //
    wprintf(L"Writing test pattern...\n");
    ZeroMemory(&Ovr, sizeof(Ovr));
    Ovr.Offset = 0x00;
    Ovr.OffsetHigh = 0;

    ZeroMemory(TestBuff, sizeof(TestBuff));
    _snwprintf(TestBuff, sizeof(TestBuff), TEST_PATTERN );

    if(!WriteFile(hDisk, TestBuff, sizeof(TestBuff), NULL, &Ovr))
        error(1, L"Error writing test pattern to disk");

    ZeroMemory(TestBuff, sizeof(TestBuff));

    if(!ReadFile( hDisk, TestBuff, sizeof(TestBuff), NULL, &Ovr ))
        error(1, L"Error reading disk");

    wprintf(L"Buffer before TRIM: \"%s\"\n", TestBuff );

    if(wcscmp(TestBuff, TEST_PATTERN)!=0)
        error(1, L"Unable to write test pattern to disk");

    //
    // UNMAP
    //
    wprintf(L"Performing UNMAP on the LBA range...\n");
    TransferSize = sizeof(UNMAP_LIST_HEADER) + sizeof(UNMAP_BLOCK_DESCRIPTOR);

    BufLen = sizeof(SCSI_PASS_THROUGH) + SENSE_INFO_LENGTH + TransferSize;

    Buffer = malloc( BufLen );
    ZeroMemory( Buffer, BufLen );

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

    pUnmapHdr->Descriptors[0].StartingLba = 0;
    pUnmapHdr->Descriptors[0].LbaCount = REVERSE_BYTES_LONG(DiskLbaCount + 1);

    if(!DeviceIoControl( hDisk, IOCTL_SCSI_PASS_THROUGH, Buffer, BufLen, Buffer, BufLen, &BytesRet, NULL ))
        error(1, L"Error performing DeviceIoControl IOCTL_SCSI_PASS_THROUGH");

    /*wprintf(L"SCSI Status: %u\n", ScsiPass->ScsiStatus );
    wprintf(L"Sense Code: ");
    for( i=0; i<32; i++ ) 
        wprintf(L"%02X ", pSenseCode[i] );
    wprintf(L"\n");*/

    ZeroMemory(TestBuff, sizeof(TestBuff));

    wprintf(L"Reading test pattern...\n");
    if(!ReadFile(hDisk, TestBuff, sizeof(TestBuff), NULL, &Ovr))
        error(1, L"Error reading disk");

    wprintf(L"Buffer after TRIM : \"%s\" [if empty, TRIM worked]\n", TestBuff);
    
    if(wcscmp(TestBuff, TEST_PATTERN)==0)
        wprintf(L"ERROR: TRIM didn't seem to work\n");
    else if(wcslen(TestBuff)==0)
        wprintf(L"Looks like TRIM worked!\n");

    return 0;
}