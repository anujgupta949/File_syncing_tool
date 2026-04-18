#include "server.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <direct.h>
#include <errno.h>
#include <io.h>
#include <limits.h>
#include <stdint.h>

#include "FileSearch.h"
#include "compare.h"
#include "cJSON.h"

#pragma comment(lib, "ws2_32.lib")

static const int DELTA_MIN_FILE_SIZE = 128 * 1024;
static const int DELTA_BLOCK_SIZE_SMALL = 16 * 1024;
static const int DELTA_BLOCK_SIZE_MEDIUM = 32 * 1024;
static const int DELTA_BLOCK_SIZE_LARGE = 64 * 1024;

struct BlockSignature {
    uint64_t Hash;
    uint32_t Size;
};

static int ChooseDeltaBlockSize(int FileSize) {
    if (FileSize < 512 * 1024) {
        return DELTA_BLOCK_SIZE_SMALL;
    }

    if (FileSize < 8 * 1024 * 1024) {
        return DELTA_BLOCK_SIZE_MEDIUM;
    }

    return DELTA_BLOCK_SIZE_LARGE;
}

static int GetBlockCount(int FileSize, int BlockSize) {
    if (FileSize <= 0 || BlockSize <= 0) {
        return 0;
    }

    return (FileSize + BlockSize - 1) / BlockSize;
}

static uint64_t HashBuffer64(const unsigned char *Data, int Length) {
    uint64_t Hash = 1469598103934665603ULL;

    for (int Index = 0; Index < Length; Index++) {
        Hash ^= (uint64_t)Data[Index];
        Hash *= 1099511628211ULL;
    }

    return Hash;
}

static bool GetFileSizeByPath(const char *AbsolutePath, int *OutSize) {
    if (AbsolutePath == NULL || OutSize == NULL) {
        return false;
    }

    *OutSize = 0;

    FILE *Fp = NULL;
    errno_t Error = fopen_s(&Fp, AbsolutePath, "rb");
    if (Error != 0 || Fp == NULL) {
        return false;
    }

    if (fseek(Fp, 0, SEEK_END) != 0) {
        fclose(Fp);
        return false;
    }

    long Size = ftell(Fp);
    fclose(Fp);

    if (Size < 0) {
        return false;
    }

    *OutSize = (int)Size;
    return true;
}

static bool EnsureDynamicCapacity(unsigned char **Buffer, int *Capacity, int RequiredCapacity) {
    if (Buffer == NULL || Capacity == NULL || RequiredCapacity < 0) {
        return false;
    }

    if (*Capacity >= RequiredCapacity) {
        return true;
    }

    int NewCapacity = (*Capacity <= 0) ? 256 : *Capacity;
    while (NewCapacity < RequiredCapacity) {
        if (NewCapacity > (INT_MAX / 2)) {
            NewCapacity = RequiredCapacity;
            break;
        }

        NewCapacity *= 2;
    }

    unsigned char *NewBuffer = (unsigned char *)realloc(*Buffer, (size_t)NewCapacity);
    if (NewBuffer == NULL) {
        return false;
    }

    *Buffer = NewBuffer;
    *Capacity = NewCapacity;
    return true;
}

static bool AppendBytes(unsigned char **Buffer,
                        int *Capacity,
                        int *Length,
                        const void *Data,
                        int DataLength) {
    if (Buffer == NULL || Capacity == NULL || Length == NULL || DataLength < 0) {
        return false;
    }

    if (DataLength == 0) {
        return true;
    }

    int RequiredCapacity = *Length + DataLength;
    if (RequiredCapacity < *Length || !EnsureDynamicCapacity(Buffer, Capacity, RequiredCapacity)) {
        return false;
    }

    memcpy(*Buffer + *Length, Data, (size_t)DataLength);
    *Length += DataLength;
    return true;
}

static bool AppendU32(unsigned char **Buffer,
                      int *Capacity,
                      int *Length,
                      uint32_t Value) {
    return AppendBytes(Buffer, Capacity, Length, &Value, (int)sizeof(Value));
}

static bool ReadU32(const unsigned char *Buffer,
                    int BufferLength,
                    int *Offset,
                    uint32_t *OutValue) {
    if (Buffer == NULL || Offset == NULL || OutValue == NULL) {
        return false;
    }

    if (*Offset < 0 || *Offset > BufferLength - (int)sizeof(uint32_t)) {
        return false;
    }

    memcpy(OutValue, Buffer + *Offset, sizeof(uint32_t));
    *Offset += (int)sizeof(uint32_t);
    return true;
}

static bool SendAll(SOCKET Socket, const char *Buffer, int Length) {
    int Sent = 0;
    while (Sent < Length) {
        int N = send(Socket, Buffer + Sent, Length - Sent, 0);
        if (N == SOCKET_ERROR || N == 0) {
            return false;
        }
        Sent += N;
    }
    return true;
}

static bool RecvAll(SOCKET Socket, char *Buffer, int Length) {
    int Received = 0;
    while (Received < Length) {
        int N = recv(Socket, Buffer + Received, Length - Received, 0);
        if (N == SOCKET_ERROR || N == 0) {
            return false;
        }
        Received += N;
    }
    return true;
}

static bool SendBlob(SOCKET Socket, const unsigned char *Data, int Length) {
    int NetworkLength = htonl(Length);
    if (!SendAll(Socket, (const char *)&NetworkLength, (int)sizeof(NetworkLength))) {
        return false;
    }

    if (Length == 0) {
        return true;
    }

    return SendAll(Socket, (const char *)Data, Length);
}

static unsigned char *ReceiveBlob(SOCKET Socket, int *OutLength) {
    if (OutLength == NULL) {
        return NULL;
    }

    *OutLength = -1;

    int NetworkLength = 0;
    if (!RecvAll(Socket, (char *)&NetworkLength, (int)sizeof(NetworkLength))) {
        return NULL;
    }

    int Length = ntohl(NetworkLength);
    if (Length < 0) {
        return NULL;
    }

    unsigned char *Data = (unsigned char *)malloc((size_t)Length);
    if (Length > 0 && Data == NULL) {
        return NULL;
    }

    if (Length > 0 && !RecvAll(Socket, (char *)Data, Length)) {
        free(Data);
        return NULL;
    }

    *OutLength = Length;
    return Data;
}

bool SendJson(SOCKET ClientSocket, const char *JsonString) {
    if (JsonString == NULL) {
        return false;
    }

    int PayloadLength = (int)strlen(JsonString);
    int NetworkLength = htonl(PayloadLength);

    if (!SendAll(ClientSocket, (const char *)&NetworkLength, (int)sizeof(NetworkLength))) {
        return false;
    }

    if (PayloadLength == 0) {
        return true;
    }

    return SendAll(ClientSocket, JsonString, PayloadLength);
}

char *ReceiveJson(SOCKET ClientSocket) {
    int NetworkLength = 0;
    if (!RecvAll(ClientSocket, (char *)&NetworkLength, (int)sizeof(NetworkLength))) {
        return NULL;
    }

    int PayloadLength = ntohl(NetworkLength);
    if (PayloadLength < 0) {
        return NULL;
    }

    char *Json = (char *)malloc((size_t)PayloadLength + 1);
    if (Json == NULL) {
        return NULL;
    }

    if (PayloadLength > 0 && !RecvAll(ClientSocket, Json, PayloadLength)) {
        free(Json);
        return NULL;
    }

    Json[PayloadLength] = '\0';
    return Json;
}

static bool InitWinsock() {
    WSADATA WinSockData;
    int Result = WSAStartup(MAKEWORD(2, 2), &WinSockData);
    return (Result == 0);
}

static SOCKET ConnectSocketByAddress(const char *ServerName, const char *Port) {
    struct addrinfo Hints;
    struct addrinfo *Result = NULL;

    ZeroMemory(&Hints, sizeof(Hints));
    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(ServerName, Port, &Hints, &Result) != 0) {
        return INVALID_SOCKET;
    }

    SOCKET ConnectSocket = INVALID_SOCKET;

    for (struct addrinfo *Ptr = Result; Ptr != NULL; Ptr = Ptr->ai_next) {
        ConnectSocket = socket(Ptr->ai_family, Ptr->ai_socktype, Ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            continue;
        }

        if (connect(ConnectSocket, Ptr->ai_addr, (int)Ptr->ai_addrlen) == 0) {
            break;
        }

        closesocket(ConnectSocket);
        ConnectSocket = INVALID_SOCKET;
    }

    freeaddrinfo(Result);
    return ConnectSocket;
}

static SOCKET CreateListeningSocket(const char *BindAddress, const char *Port) {
    struct addrinfo Hints;
    struct addrinfo *Result = NULL;

    ZeroMemory(&Hints, sizeof(Hints));
    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_protocol = IPPROTO_TCP;
    Hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(BindAddress, Port, &Hints, &Result) != 0) {
        return INVALID_SOCKET;
    }

    SOCKET ListenSocket = INVALID_SOCKET;

    for (struct addrinfo *Ptr = Result; Ptr != NULL; Ptr = Ptr->ai_next) {
        ListenSocket = socket(Ptr->ai_family, Ptr->ai_socktype, Ptr->ai_protocol);
        if (ListenSocket == INVALID_SOCKET) {
            continue;
        }

        if (bind(ListenSocket, Ptr->ai_addr, (int)Ptr->ai_addrlen) == SOCKET_ERROR) {
            closesocket(ListenSocket);
            ListenSocket = INVALID_SOCKET;
            continue;
        }

        if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(ListenSocket);
            ListenSocket = INVALID_SOCKET;
            continue;
        }

        break;
    }

    freeaddrinfo(Result);
    return ListenSocket;
}

static bool IsSafeRelativePath(const char *Path) {
    if (Path == NULL || Path[0] == '\0') {
        return false;
    }

    if (strstr(Path, "..") != NULL) {
        return false;
    }

    if (Path[0] == '\\' || Path[0] == '/' || strchr(Path, ':') != NULL) {
        return false;
    }

    return true;
}

static bool BuildAbsolutePath(const char *RootFolder, const char *RelativePath, char *OutPath, size_t OutPathSize) {
    if (RootFolder == NULL || RelativePath == NULL || OutPath == NULL) {
        return false;
    }

    if (!IsSafeRelativePath(RelativePath)) {
        return false;
    }

    int N = snprintf(OutPath, OutPathSize, "%s\\%s", RootFolder, RelativePath);
    if (N < 0 || (size_t)N >= OutPathSize) {
        return false;
    }

    return true;
}

static void EnsureParentDirs(const char *AbsolutePath) {
    char Temp[MAX_PATH];
    strncpy_s(Temp, AbsolutePath, MAX_PATH - 1);
    Temp[MAX_PATH - 1] = '\0';

    for (char *P = Temp; *P != '\0'; P++) {
        if (*P == '\\' || *P == '/') {
            char Saved = *P;
            *P = '\0';
            if (strlen(Temp) > 2) {
                _mkdir(Temp);
            }
            *P = Saved;
        }
    }
}

static unsigned char *ReadFileBinary(const char *AbsolutePath, int *OutLength) {
    *OutLength = 0;

    FILE *Fp = NULL;
    errno_t Error = fopen_s(&Fp, AbsolutePath, "rb");
    if (Error != 0 || Fp == NULL) {
        return NULL;
    }

    if (fseek(Fp, 0, SEEK_END) != 0) {
        fclose(Fp);
        return NULL;
    }

    long Size = ftell(Fp);
    if (Size < 0) {
        fclose(Fp);
        return NULL;
    }

    rewind(Fp);

    size_t AllocationSize = (Size > 0) ? (size_t)Size : 1;
    unsigned char *Buffer = (unsigned char *)malloc(AllocationSize);
    if (Buffer == NULL) {
        fclose(Fp);
        return NULL;
    }

    if (Size > 0) {
        size_t ReadCount = fread(Buffer, 1, (size_t)Size, Fp);
        if (ReadCount != (size_t)Size) {
            free(Buffer);
            fclose(Fp);
            return NULL;
        }
    }

    fclose(Fp);
    *OutLength = (int)Size;
    return Buffer;
}

static bool WriteFileBinary(const char *AbsolutePath, const unsigned char *Data, int Length) {
    EnsureParentDirs(AbsolutePath);

    FILE *Fp = NULL;
    errno_t Error = fopen_s(&Fp, AbsolutePath, "wb");
    if (Error != 0 || Fp == NULL) {
        return false;
    }

    if (Length > 0) {
        size_t WriteCount = fwrite(Data, 1, (size_t)Length, Fp);
        fclose(Fp);
        return WriteCount == (size_t)Length;
    }

    fclose(Fp);
    return true;
}

static char *BuildCommandJsonEx(const char *Cmd,
                                const char *Path,
                                int Size,
                                int BlockSize,
                                int BlockCount,
                                int ChangedCount) {
    cJSON *Obj = cJSON_CreateObject();
    if (Obj == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(Obj, "cmd", Cmd);
    if (Path != NULL) {
        cJSON_AddStringToObject(Obj, "path", Path);
    }

    if (Size >= 0) {
        char SizeStr[32];
        _itoa_s(Size, SizeStr, sizeof(SizeStr), 10);
        cJSON_AddStringToObject(Obj, "size", SizeStr);
    }

    if (BlockSize > 0) {
        char BlockSizeStr[32];
        _itoa_s(BlockSize, BlockSizeStr, sizeof(BlockSizeStr), 10);
        cJSON_AddStringToObject(Obj, "blockSize", BlockSizeStr);
    }

    if (BlockCount >= 0) {
        char BlockCountStr[32];
        _itoa_s(BlockCount, BlockCountStr, sizeof(BlockCountStr), 10);
        cJSON_AddStringToObject(Obj, "blockCount", BlockCountStr);
    }

    if (ChangedCount >= 0) {
        char ChangedCountStr[32];
        _itoa_s(ChangedCount, ChangedCountStr, sizeof(ChangedCountStr), 10);
        cJSON_AddStringToObject(Obj, "changedCount", ChangedCountStr);
    }

    char *Json = cJSON_PrintUnformatted(Obj);
    cJSON_Delete(Obj);
    return Json;
}

static char *BuildCommandJson(const char *Cmd, const char *Path, int Size) {
    return BuildCommandJsonEx(Cmd, Path, Size, -1, -1, -1);
}

static int ParseOptionalIntField(cJSON *Obj, const char *FieldName, int DefaultValue) {
    cJSON *Field = cJSON_GetObjectItemCaseSensitive(Obj, FieldName);
    if (Field == NULL) {
        return DefaultValue;
    }

    if (cJSON_IsString(Field) && Field->valuestring != NULL) {
        return atoi(Field->valuestring);
    }

    if (cJSON_IsNumber(Field)) {
        return (int)Field->valuedouble;
    }

    return DefaultValue;
}

static bool ParseCommandEx(const char *Json,
                           char *Cmd,
                           size_t CmdCap,
                           char *Path,
                           size_t PathCap,
                           int *Size,
                           int *BlockSize,
                           int *BlockCount,
                           int *ChangedCount) {
    if (Json == NULL || Cmd == NULL || Path == NULL || Size == NULL) {
        return false;
    }

    Cmd[0] = '\0';
    Path[0] = '\0';
    *Size = -1;
    if (BlockSize != NULL) {
        *BlockSize = -1;
    }
    if (BlockCount != NULL) {
        *BlockCount = -1;
    }
    if (ChangedCount != NULL) {
        *ChangedCount = -1;
    }

    cJSON *Obj = cJSON_Parse(Json);
    if (Obj == NULL) {
        return false;
    }

    cJSON *CmdItem = cJSON_GetObjectItemCaseSensitive(Obj, "cmd");
    cJSON *PathItem = cJSON_GetObjectItemCaseSensitive(Obj, "path");
    if (!cJSON_IsString(CmdItem) || CmdItem->valuestring == NULL) {
        cJSON_Delete(Obj);
        return false;
    }

    strncpy_s(Cmd, CmdCap, CmdItem->valuestring, _TRUNCATE);

    if (cJSON_IsString(PathItem) && PathItem->valuestring != NULL) {
        strncpy_s(Path, PathCap, PathItem->valuestring, _TRUNCATE);
    }

    *Size = ParseOptionalIntField(Obj, "size", -1);
    if (BlockSize != NULL) {
        *BlockSize = ParseOptionalIntField(Obj, "blockSize", -1);
    }
    if (BlockCount != NULL) {
        *BlockCount = ParseOptionalIntField(Obj, "blockCount", -1);
    }
    if (ChangedCount != NULL) {
        *ChangedCount = ParseOptionalIntField(Obj, "changedCount", -1);
    }

    cJSON_Delete(Obj);
    return true;
}

static bool ParseCommand(const char *Json,
                         char *Cmd,
                         size_t CmdCap,
                         char *Path,
                         size_t PathCap,
                         int *Size) {
    return ParseCommandEx(Json, Cmd, CmdCap, Path, PathCap, Size, NULL, NULL, NULL);
}

static bool BuildSignatureBlobForFile(const char *AbsolutePath,
                                      int BlockSize,
                                      unsigned char **OutBlob,
                                      int *OutBlobLength,
                                      int *OutFileSize,
                                      int *OutBlockCount) {
    if (AbsolutePath == NULL ||
        BlockSize <= 0 ||
        OutBlob == NULL ||
        OutBlobLength == NULL ||
        OutFileSize == NULL ||
        OutBlockCount == NULL) {
        return false;
    }

    *OutBlob = NULL;
    *OutBlobLength = 0;
    *OutFileSize = 0;
    *OutBlockCount = 0;

    int FileSize = 0;
    if (!GetFileSizeByPath(AbsolutePath, &FileSize)) {
        return false;
    }

    FILE *Fp = NULL;
    errno_t Error = fopen_s(&Fp, AbsolutePath, "rb");
    if (Error != 0 || Fp == NULL) {
        return false;
    }

    int BlockCount = GetBlockCount(FileSize, BlockSize);
    int BlobLength = BlockCount * (int)(sizeof(uint64_t) + sizeof(uint32_t));
    size_t AllocationSize = (BlobLength > 0) ? (size_t)BlobLength : 1;
    unsigned char *Blob = (unsigned char *)malloc(AllocationSize);
    unsigned char *BlockBuffer = (unsigned char *)malloc((size_t)BlockSize);
    if (Blob == NULL || BlockBuffer == NULL) {
        free(Blob);
        free(BlockBuffer);
        fclose(Fp);
        return false;
    }

    for (int BlockIndex = 0; BlockIndex < BlockCount; BlockIndex++) {
        int BlockOffset = BlockIndex * BlockSize;
        int BlockLength = FileSize - BlockOffset;
        if (BlockLength > BlockSize) {
            BlockLength = BlockSize;
        }

        size_t ReadCount = fread(BlockBuffer, 1, (size_t)BlockLength, Fp);
        if (ReadCount != (size_t)BlockLength) {
            free(Blob);
            free(BlockBuffer);
            fclose(Fp);
            return false;
        }

        uint64_t Hash = HashBuffer64(BlockBuffer, BlockLength);
        uint32_t StoredSize = (uint32_t)BlockLength;
        int EntryOffset = BlockIndex * (int)(sizeof(uint64_t) + sizeof(uint32_t));
        memcpy(Blob + EntryOffset, &Hash, sizeof(Hash));
        memcpy(Blob + EntryOffset + (int)sizeof(Hash), &StoredSize, sizeof(StoredSize));
    }

    free(BlockBuffer);
    fclose(Fp);

    *OutBlob = Blob;
    *OutBlobLength = BlobLength;
    *OutFileSize = FileSize;
    *OutBlockCount = BlockCount;
    return true;
}

static bool ParseSignatureBlob(const unsigned char *Blob,
                               int BlobLength,
                               int BlockCount,
                               struct BlockSignature **OutSignatures) {
    if (BlockCount < 0 || OutSignatures == NULL) {
        return false;
    }

    *OutSignatures = NULL;

    int ExpectedLength = BlockCount * (int)(sizeof(uint64_t) + sizeof(uint32_t));
    if (BlobLength != ExpectedLength) {
        return false;
    }

    if (BlockCount == 0) {
        return true;
    }

    struct BlockSignature *Signatures =
        (struct BlockSignature *)malloc((size_t)BlockCount * sizeof(struct BlockSignature));
    if (Signatures == NULL) {
        return false;
    }

    for (int BlockIndex = 0; BlockIndex < BlockCount; BlockIndex++) {
        int EntryOffset = BlockIndex * (int)(sizeof(uint64_t) + sizeof(uint32_t));
        memcpy(&Signatures[BlockIndex].Hash, Blob + EntryOffset, sizeof(uint64_t));
        memcpy(&Signatures[BlockIndex].Size,
               Blob + EntryOffset + (int)sizeof(uint64_t),
               sizeof(uint32_t));
    }

    *OutSignatures = Signatures;
    return true;
}

static bool BuildDeltaPatchForFile(const char *AbsolutePath,
                                   const struct BlockSignature *TargetSignatures,
                                   int TargetBlockCount,
                                   int BlockSize,
                                   unsigned char **OutPatchBlob,
                                   int *OutPatchLength,
                                   int *OutFileSize,
                                   int *OutChangedCount) {
    if (AbsolutePath == NULL ||
        TargetBlockCount < 0 ||
        BlockSize <= 0 ||
        OutPatchBlob == NULL ||
        OutPatchLength == NULL ||
        OutFileSize == NULL ||
        OutChangedCount == NULL) {
        return false;
    }

    *OutPatchBlob = NULL;
    *OutPatchLength = 0;
    *OutFileSize = 0;
    *OutChangedCount = 0;

    int FileSize = 0;
    if (!GetFileSizeByPath(AbsolutePath, &FileSize)) {
        return false;
    }

    FILE *Fp = NULL;
    errno_t Error = fopen_s(&Fp, AbsolutePath, "rb");
    if (Error != 0 || Fp == NULL) {
        return false;
    }

    unsigned char *BlockBuffer = (unsigned char *)malloc((size_t)BlockSize);
    if (BlockBuffer == NULL) {
        fclose(Fp);
        return false;
    }

    unsigned char *PatchBlob = NULL;
    int PatchCapacity = 0;
    int PatchLength = 0;
    int ChangedCount = 0;

    int BlockCount = GetBlockCount(FileSize, BlockSize);
    for (int BlockIndex = 0; BlockIndex < BlockCount; BlockIndex++) {
        int BlockOffset = BlockIndex * BlockSize;
        int BlockLength = FileSize - BlockOffset;
        if (BlockLength > BlockSize) {
            BlockLength = BlockSize;
        }

        size_t ReadCount = fread(BlockBuffer, 1, (size_t)BlockLength, Fp);
        if (ReadCount != (size_t)BlockLength) {
            free(PatchBlob);
            free(BlockBuffer);
            fclose(Fp);
            return false;
        }

        uint64_t Hash = HashBuffer64(BlockBuffer, BlockLength);
        bool MatchesTarget =
            (BlockIndex < TargetBlockCount) &&
            (TargetSignatures != NULL) &&
            (TargetSignatures[BlockIndex].Size == (uint32_t)BlockLength) &&
            (TargetSignatures[BlockIndex].Hash == Hash);

        if (MatchesTarget) {
            continue;
        }

        if (!AppendU32(&PatchBlob, &PatchCapacity, &PatchLength, (uint32_t)BlockIndex) ||
            !AppendU32(&PatchBlob, &PatchCapacity, &PatchLength, (uint32_t)BlockLength) ||
            !AppendBytes(&PatchBlob, &PatchCapacity, &PatchLength, BlockBuffer, BlockLength)) {
            free(PatchBlob);
            free(BlockBuffer);
            fclose(Fp);
            return false;
        }

        ChangedCount++;
    }

    free(BlockBuffer);
    fclose(Fp);

    *OutPatchBlob = PatchBlob;
    *OutPatchLength = PatchLength;
    *OutFileSize = FileSize;
    *OutChangedCount = ChangedCount;
    return true;
}

static bool ShouldUseDeltaTransfer(int FileSize, int PatchLength) {
    if (FileSize < DELTA_MIN_FILE_SIZE) {
        return false;
    }

    return (PatchLength + 64) < FileSize;
}

static bool ApplyDeltaPatchByRelativePath(const char *RootFolder,
                                          const char *RelativePath,
                                          int FinalSize,
                                          int BlockSize,
                                          int ChangedCount,
                                          const unsigned char *PatchBlob,
                                          int PatchLength) {
    if (FinalSize < 0 || BlockSize <= 0 || ChangedCount < 0) {
        return false;
    }

    char AbsPath[MAX_PATH];
    if (!BuildAbsolutePath(RootFolder, RelativePath, AbsPath, sizeof(AbsPath))) {
        return false;
    }

    EnsureParentDirs(AbsPath);

    FILE *Fp = NULL;
    errno_t Error = fopen_s(&Fp, AbsPath, "r+b");
    if (Error != 0 || Fp == NULL) {
        Error = fopen_s(&Fp, AbsPath, "w+b");
        if (Error != 0 || Fp == NULL) {
            return false;
        }
    }

    int Offset = 0;
    for (int ChangeIndex = 0; ChangeIndex < ChangedCount; ChangeIndex++) {
        uint32_t BlockIndex = 0;
        uint32_t DataLength = 0;
        if (!ReadU32(PatchBlob, PatchLength, &Offset, &BlockIndex) ||
            !ReadU32(PatchBlob, PatchLength, &Offset, &DataLength) ||
            DataLength > (uint32_t)BlockSize ||
            Offset > PatchLength - (int)DataLength) {
            fclose(Fp);
            return false;
        }

        __int64 FileOffset = (__int64)BlockIndex * (__int64)BlockSize;
        if (_fseeki64(Fp, FileOffset, SEEK_SET) != 0) {
            fclose(Fp);
            return false;
        }

        if (DataLength > 0) {
            size_t WriteCount = fwrite(PatchBlob + Offset, 1, (size_t)DataLength, Fp);
            if (WriteCount != (size_t)DataLength) {
                fclose(Fp);
                return false;
            }
            Offset += (int)DataLength;
        }
    }

    if (Offset != PatchLength || fflush(Fp) != 0) {
        fclose(Fp);
        return false;
    }

    bool Ok = (_chsize_s(_fileno(Fp), (__int64)FinalSize) == 0);
    fclose(Fp);
    return Ok;
}

static bool SendFileByRelativePathWithCommand(SOCKET Socket,
                                              const char *RootFolder,
                                              const char *RelativePath,
                                              const char *OkCommand) {
    char AbsPath[MAX_PATH];
    if (!BuildAbsolutePath(RootFolder, RelativePath, AbsPath, sizeof(AbsPath))) {
        char *Err = BuildCommandJson("ERR", "invalid path", -1);
        bool Ok = (Err != NULL) ? SendJson(Socket, Err) : false;
        free(Err);
        return Ok;
    }

    int Size = 0;
    unsigned char *Data = ReadFileBinary(AbsPath, &Size);
    if (Data == NULL) {
        char *Err = BuildCommandJson("ERR", "file not found", -1);
        bool Ok = (Err != NULL) ? SendJson(Socket, Err) : false;
        free(Err);
        return Ok;
    }

    char *OkJson = BuildCommandJson(OkCommand, RelativePath, Size);
    bool Ok = (OkJson != NULL) ? SendJson(Socket, OkJson) : false;
    free(OkJson);

    if (Ok) {
        Ok = SendBlob(Socket, Data, Size);
    }

    free(Data);
    return Ok;
}

static bool SendFileByRelativePath(SOCKET Socket, const char *RootFolder, const char *RelativePath) {
    return SendFileByRelativePathWithCommand(Socket, RootFolder, RelativePath, "OK");
}

static bool ReceiveAndWriteFileByRelativePath(SOCKET Socket, const char *RootFolder, const char *RelativePath, int Size) {
    if (Size < 0) {
        return false;
    }

    char AbsPath[MAX_PATH];
    if (!BuildAbsolutePath(RootFolder, RelativePath, AbsPath, sizeof(AbsPath))) {
        return false;
    }

    int PayloadLen = 0;
    unsigned char *Payload = ReceiveBlob(Socket, &PayloadLen);
    if (Payload == NULL && PayloadLen != 0) {
        return false;
    }

    bool Ok = (PayloadLen == Size) && WriteFileBinary(AbsPath, Payload, PayloadLen);
    free(Payload);
    return Ok;
}

static bool SendSignatureByRelativePath(SOCKET Socket,
                                        const char *RootFolder,
                                        const char *RelativePath,
                                        int BlockSize) {
    char AbsPath[MAX_PATH];
    if (!BuildAbsolutePath(RootFolder, RelativePath, AbsPath, sizeof(AbsPath))) {
        char *Err = BuildCommandJson("ERR", "invalid path", -1);
        bool Ok = (Err != NULL) ? SendJson(Socket, Err) : false;
        free(Err);
        return Ok;
    }

    int FileSize = 0;
    if (!GetFileSizeByPath(AbsPath, &FileSize) || FileSize < DELTA_MIN_FILE_SIZE) {
        char *NoSig = BuildCommandJson("NOSIG", RelativePath, FileSize);
        bool Ok = (NoSig != NULL) ? SendJson(Socket, NoSig) : false;
        free(NoSig);
        return Ok;
    }

    unsigned char *Blob = NULL;
    int BlobLength = 0;
    int BlockCount = 0;
    if (!BuildSignatureBlobForFile(AbsPath, BlockSize, &Blob, &BlobLength, &FileSize, &BlockCount)) {
        char *Err = BuildCommandJson("ERR", "signature build failed", -1);
        bool Ok = (Err != NULL) ? SendJson(Socket, Err) : false;
        free(Err);
        return Ok;
    }

    char *OkJson = BuildCommandJsonEx("OKSIG", RelativePath, FileSize, BlockSize, BlockCount, -1);
    bool Ok = (OkJson != NULL) ? SendJson(Socket, OkJson) : false;
    free(OkJson);

    if (Ok) {
        Ok = SendBlob(Socket, Blob, BlobLength);
    }

    free(Blob);
    return Ok;
}

static bool SendDeltaOrFullFileByRelativePath(SOCKET Socket,
                                              const char *RootFolder,
                                              const char *RelativePath,
                                              int BlockSize,
                                              int BlockCount,
                                              const unsigned char *SignatureBlob,
                                              int SignatureBlobLength) {
    char AbsPath[MAX_PATH];
    if (!BuildAbsolutePath(RootFolder, RelativePath, AbsPath, sizeof(AbsPath))) {
        char *Err = BuildCommandJson("ERR", "invalid path", -1);
        bool Ok = (Err != NULL) ? SendJson(Socket, Err) : false;
        free(Err);
        return Ok;
    }

    int FileSize = 0;
    if (!GetFileSizeByPath(AbsPath, &FileSize)) {
        char *Err = BuildCommandJson("ERR", "file not found", -1);
        bool Ok = (Err != NULL) ? SendJson(Socket, Err) : false;
        free(Err);
        return Ok;
    }

    if (FileSize < DELTA_MIN_FILE_SIZE || BlockSize <= 0 || BlockCount < 0) {
        return SendFileByRelativePathWithCommand(Socket, RootFolder, RelativePath, "OKFULL");
    }

    struct BlockSignature *Signatures = NULL;
    if (!ParseSignatureBlob(SignatureBlob, SignatureBlobLength, BlockCount, &Signatures)) {
        char *Err = BuildCommandJson("ERR", "invalid signatures", -1);
        bool Ok = (Err != NULL) ? SendJson(Socket, Err) : false;
        free(Err);
        return Ok;
    }

    unsigned char *PatchBlob = NULL;
    int PatchLength = 0;
    int ChangedCount = 0;
    bool PatchReady = BuildDeltaPatchForFile(
        AbsPath, Signatures, BlockCount, BlockSize, &PatchBlob, &PatchLength, &FileSize, &ChangedCount);
    free(Signatures);

    if (!PatchReady || !ShouldUseDeltaTransfer(FileSize, PatchLength)) {
        free(PatchBlob);
        return SendFileByRelativePathWithCommand(Socket, RootFolder, RelativePath, "OKFULL");
    }

    char *OkJson = BuildCommandJsonEx("OKDELTA", RelativePath, FileSize, BlockSize, BlockCount, ChangedCount);
    bool Ok = (OkJson != NULL) ? SendJson(Socket, OkJson) : false;
    free(OkJson);

    if (Ok) {
        Ok = SendBlob(Socket, PatchBlob, PatchLength);
    }

    free(PatchBlob);
    return Ok;
}

static bool ProcessServerCommands(SOCKET ClientSocket, const char *ServerRootFolder) {
    for (;;) {
        char *ReqJson = ReceiveJson(ClientSocket);
        if (ReqJson == NULL) {
            return false;
        }

        char Cmd[32];
        char Path[MAX_PATH];
        int Size = -1;
        int BlockSize = -1;
        int BlockCount = -1;
        int ChangedCount = -1;

        bool Parsed = ParseCommandEx(
            ReqJson,
            Cmd,
            sizeof(Cmd),
            Path,
            sizeof(Path),
            &Size,
            &BlockSize,
            &BlockCount,
            &ChangedCount);
        free(ReqJson);

        if (!Parsed) {
            return false;
        }

        if (strcmp(Cmd, "DONE") == 0) {
            return true;
        }

        if (strcmp(Cmd, "GET") == 0) {
            if (!SendFileByRelativePath(ClientSocket, ServerRootFolder, Path)) {
                return false;
            }
            continue;
        }

        if (strcmp(Cmd, "SIG") == 0) {
            if (!SendSignatureByRelativePath(ClientSocket, ServerRootFolder, Path, BlockSize)) {
                return false;
            }
            continue;
        }

        if (strcmp(Cmd, "DGET") == 0) {
            int SignatureBlobLength = 0;
            unsigned char *SignatureBlob = ReceiveBlob(ClientSocket, &SignatureBlobLength);
            bool Ok = (SignatureBlob != NULL || SignatureBlobLength == 0) &&
                      SendDeltaOrFullFileByRelativePath(
                          ClientSocket,
                          ServerRootFolder,
                          Path,
                          BlockSize,
                          BlockCount,
                          SignatureBlob,
                          SignatureBlobLength);
            free(SignatureBlob);
            if (!Ok) {
                return false;
            }
            continue;
        }

        if (strcmp(Cmd, "PUT") == 0) {
            bool Ok = ReceiveAndWriteFileByRelativePath(ClientSocket, ServerRootFolder, Path, Size);
            char *Resp = BuildCommandJson(Ok ? "OK" : "ERR", Path, -1);
            bool Sent = (Resp != NULL) ? SendJson(ClientSocket, Resp) : false;
            free(Resp);
            if (!Sent) {
                return false;
            }
            continue;
        }

        if (strcmp(Cmd, "DPUT") == 0) {
            int PatchLength = 0;
            unsigned char *PatchBlob = ReceiveBlob(ClientSocket, &PatchLength);
            bool Ok = (PatchBlob != NULL || PatchLength == 0) &&
                      ApplyDeltaPatchByRelativePath(
                          ServerRootFolder,
                          Path,
                          Size,
                          BlockSize,
                          ChangedCount,
                          PatchBlob,
                          PatchLength);
            free(PatchBlob);

            char *Resp = BuildCommandJson(Ok ? "OK" : "ERR", Path, -1);
            bool Sent = (Resp != NULL) ? SendJson(ClientSocket, Resp) : false;
            free(Resp);
            if (!Sent) {
                return false;
            }
            continue;
        }

        char *Err = BuildCommandJson("ERR", "unknown command", -1);
        bool Sent = (Err != NULL) ? SendJson(ClientSocket, Err) : false;
        free(Err);
        if (!Sent) {
            return false;
        }
    }
}

static bool DownloadFromServer(SOCKET Socket, const char *RootFolder, const char *RelativePath) {
    char AbsPath[MAX_PATH];
    bool HasBaseFile = BuildAbsolutePath(RootFolder, RelativePath, AbsPath, sizeof(AbsPath));
    int LocalSize = 0;
    int BlockSize = -1;
    int BlockCount = -1;
    unsigned char *SignatureBlob = NULL;
    int SignatureBlobLength = 0;

    bool CanUseDelta = HasBaseFile &&
                       GetFileSizeByPath(AbsPath, &LocalSize) &&
                       LocalSize >= DELTA_MIN_FILE_SIZE;

    if (CanUseDelta) {
        BlockSize = ChooseDeltaBlockSize(LocalSize);
        CanUseDelta = BuildSignatureBlobForFile(
            AbsPath, BlockSize, &SignatureBlob, &SignatureBlobLength, &LocalSize, &BlockCount);
    }

    char *Req = CanUseDelta
                    ? BuildCommandJsonEx("DGET", RelativePath, LocalSize, BlockSize, BlockCount, -1)
                    : BuildCommandJson("GET", RelativePath, -1);
    if (Req == NULL || !SendJson(Socket, Req)) {
        free(SignatureBlob);
        free(Req);
        return false;
    }
    free(Req);

    if (CanUseDelta && !SendBlob(Socket, SignatureBlob, SignatureBlobLength)) {
        free(SignatureBlob);
        return false;
    }
    free(SignatureBlob);

    char *RespJson = ReceiveJson(Socket);
    if (RespJson == NULL) {
        return false;
    }

    char Cmd[32];
    char Path[MAX_PATH];
    int Size = -1;
    int RespBlockSize = -1;
    int RespBlockCount = -1;
    int ChangedCount = -1;
    bool Parsed = ParseCommandEx(
        RespJson,
        Cmd,
        sizeof(Cmd),
        Path,
        sizeof(Path),
        &Size,
        &RespBlockSize,
        &RespBlockCount,
        &ChangedCount);
    free(RespJson);

    if (!Parsed) {
        return false;
    }

    if (strcmp(Cmd, "OKDELTA") == 0) {
        int PatchLength = 0;
        unsigned char *PatchBlob = ReceiveBlob(Socket, &PatchLength);
        if (PatchBlob == NULL && PatchLength != 0) {
            return false;
        }

        bool Ok = ApplyDeltaPatchByRelativePath(
            RootFolder, RelativePath, Size, RespBlockSize, ChangedCount, PatchBlob, PatchLength);
        free(PatchBlob);
        if (Ok) {
            printf("    [DELTA_DOWNLOAD] %s (%d changed blocks)\n", RelativePath, ChangedCount);
        }
        return Ok;
    }

    if (strcmp(Cmd, "OKFULL") == 0 || strcmp(Cmd, "OK") == 0) {
        return ReceiveAndWriteFileByRelativePath(Socket, RootFolder, RelativePath, Size);
    }

    return false;
}

static bool UploadToServer(SOCKET Socket, const char *RootFolder, const char *RelativePath) {
    char AbsPath[MAX_PATH];
    if (!BuildAbsolutePath(RootFolder, RelativePath, AbsPath, sizeof(AbsPath))) {
        return false;
    }

    int Size = 0;
    if (!GetFileSizeByPath(AbsPath, &Size)) {
        return false;
    }

    if (Size >= DELTA_MIN_FILE_SIZE) {
        int RequestedBlockSize = ChooseDeltaBlockSize(Size);
        char *SigReq = BuildCommandJsonEx("SIG", RelativePath, -1, RequestedBlockSize, -1, -1);
        if (SigReq == NULL || !SendJson(Socket, SigReq)) {
            free(SigReq);
            return false;
        }
        free(SigReq);

        char *SigRespJson = ReceiveJson(Socket);
        if (SigRespJson == NULL) {
            return false;
        }

        char SigCmd[32];
        char SigPath[MAX_PATH];
        int SigSize = -1;
        int SigBlockSize = -1;
        int SigBlockCount = -1;
        int SigChangedCount = -1;
        bool ParsedSig = ParseCommandEx(
            SigRespJson,
            SigCmd,
            sizeof(SigCmd),
            SigPath,
            sizeof(SigPath),
            &SigSize,
            &SigBlockSize,
            &SigBlockCount,
            &SigChangedCount);
        free(SigRespJson);

        if (!ParsedSig) {
            return false;
        }

        if (strcmp(SigCmd, "OKSIG") == 0) {
            int SignatureBlobLength = 0;
            unsigned char *SignatureBlob = ReceiveBlob(Socket, &SignatureBlobLength);
            if (SignatureBlob == NULL && SignatureBlobLength != 0) {
                return false;
            }

            struct BlockSignature *ServerSignatures = NULL;
            bool ParsedBlob = ParseSignatureBlob(
                SignatureBlob, SignatureBlobLength, SigBlockCount, &ServerSignatures);
            free(SignatureBlob);
            if (!ParsedBlob) {
                return false;
            }

            unsigned char *PatchBlob = NULL;
            int PatchLength = 0;
            int FinalSize = 0;
            int ChangedCount = 0;
            bool PatchReady = BuildDeltaPatchForFile(
                AbsPath,
                ServerSignatures,
                SigBlockCount,
                SigBlockSize,
                &PatchBlob,
                &PatchLength,
                &FinalSize,
                &ChangedCount);
            free(ServerSignatures);

            if (PatchReady && ShouldUseDeltaTransfer(FinalSize, PatchLength)) {
                char *PatchReq = BuildCommandJsonEx(
                    "DPUT", RelativePath, FinalSize, SigBlockSize, SigBlockCount, ChangedCount);
                if (PatchReq == NULL || !SendJson(Socket, PatchReq)) {
                    free(PatchBlob);
                    free(PatchReq);
                    return false;
                }
                free(PatchReq);

                if (!SendBlob(Socket, PatchBlob, PatchLength)) {
                    free(PatchBlob);
                    return false;
                }
                free(PatchBlob);

                char *PatchRespJson = ReceiveJson(Socket);
                if (PatchRespJson == NULL) {
                    return false;
                }

                char PatchCmd[32];
                char PatchPath[MAX_PATH];
                int PatchSize = -1;
                bool ParsedPatchResp =
                    ParseCommand(PatchRespJson, PatchCmd, sizeof(PatchCmd), PatchPath, sizeof(PatchPath), &PatchSize);
                free(PatchRespJson);

                if (ParsedPatchResp && strcmp(PatchCmd, "OK") == 0) {
                    printf("    [DELTA_UPLOAD] %s (%d changed blocks)\n", RelativePath, ChangedCount);
                }
                return ParsedPatchResp && strcmp(PatchCmd, "OK") == 0;
            }

            free(PatchBlob);
        } else if (strcmp(SigCmd, "ERR") == 0) {
            return false;
        }
    }

    unsigned char *Data = ReadFileBinary(AbsPath, &Size);
    if (Data == NULL) {
        return false;
    }

    char *Req = BuildCommandJson("PUT", RelativePath, Size);
    if (Req == NULL || !SendJson(Socket, Req)) {
        free(Req);
        free(Data);
        return false;
    }
    free(Req);

    if (!SendBlob(Socket, Data, Size)) {
        free(Data);
        return false;
    }
    free(Data);

    char *RespJson = ReceiveJson(Socket);
    if (RespJson == NULL) {
        return false;
    }

    char Cmd[32];
    char Path[MAX_PATH];
    int RespSize = -1;
    bool Parsed = ParseCommand(RespJson, Cmd, sizeof(Cmd), Path, sizeof(Path), &RespSize);
    free(RespJson);

    return Parsed && strcmp(Cmd, "OK") == 0;
}

bool InitializeClientSocket(const char *ServerName,
                            const char *Port,
                            const char *OutboundJson,
                            char **InboundJson) {
    if (ServerName == NULL || Port == NULL || OutboundJson == NULL || InboundJson == NULL) {
        return false;
    }

    *InboundJson = NULL;

    if (!InitWinsock()) {
        return false;
    }

    SOCKET ConnectSocket = ConnectSocketByAddress(ServerName, Port);
    if (ConnectSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    bool Ok = SendJson(ConnectSocket, OutboundJson);
    if (Ok) {
        *InboundJson = ReceiveJson(ConnectSocket);
        Ok = (*InboundJson != NULL);
    }

    closesocket(ConnectSocket);
    WSACleanup();
    return Ok;
}

bool InitializeServerSocket(const char *BindAddress,
                            const char *Port,
                            const char *OutboundJson,
                            char **InboundJson) {
    if (Port == NULL || OutboundJson == NULL || InboundJson == NULL) {
        return false;
    }

    *InboundJson = NULL;

    if (!InitWinsock()) {
        return false;
    }

    SOCKET ListenSocket = CreateListeningSocket(BindAddress, Port);
    if (ListenSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    SOCKET ClientSocket = accept(ListenSocket, NULL, NULL);
    if (ClientSocket == INVALID_SOCKET) {
        closesocket(ListenSocket);
        WSACleanup();
        return false;
    }

    *InboundJson = ReceiveJson(ClientSocket);
    bool Ok = (*InboundJson != NULL);

    if (Ok) {
        Ok = SendJson(ClientSocket, OutboundJson);
    }

    closesocket(ClientSocket);
    closesocket(ListenSocket);
    WSACleanup();
    return Ok;
}

bool RunSyncServerSession(const char *BindAddress,
                          const char *Port,
                          const char *ServerRootFolder) {
    if (Port == NULL || ServerRootFolder == NULL) {
        return false;
    }

    if (!InitWinsock()) {
        return false;
    }

    SOCKET ListenSocket = CreateListeningSocket(BindAddress, Port);
    if (ListenSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    SOCKET ClientSocket = accept(ListenSocket, NULL, NULL);
    if (ClientSocket == INVALID_SOCKET) {
        closesocket(ListenSocket);
        WSACleanup();
        return false;
    }

    struct snapshot ServerSnap;
    if (!InitSnapshot(&ServerSnap, 256) || !BuildSnapshotFromFolder(&ServerSnap, ServerRootFolder)) {
        closesocket(ClientSocket);
        closesocket(ListenSocket);
        WSACleanup();
        return false;
    }

    char *ServerJson = SnapshotToJson(&ServerSnap);
    if (ServerJson == NULL) {
        FreeSnapshot(&ServerSnap);
        closesocket(ClientSocket);
        closesocket(ListenSocket);
        WSACleanup();
        return false;
    }

    char *ClientSnapshotJson = ReceiveJson(ClientSocket);
    bool Ok = (ClientSnapshotJson != NULL) && SendJson(ClientSocket, ServerJson);

    if (Ok) {
        Ok = ProcessServerCommands(ClientSocket, ServerRootFolder);
    }

    free(ClientSnapshotJson);
    free(ServerJson);
    FreeSnapshot(&ServerSnap);
    closesocket(ClientSocket);
    closesocket(ListenSocket);
    WSACleanup();
    return Ok;
}

bool RunSyncClientSession(const char *ServerName,
                          const char *Port,
                          const char *ClientRootFolder) {
    if (ServerName == NULL || Port == NULL || ClientRootFolder == NULL) {
        return false;
    }

    if (!InitWinsock()) {
        return false;
    }

    SOCKET Socket = ConnectSocketByAddress(ServerName, Port);
    if (Socket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    struct snapshot Local;
    if (!InitSnapshot(&Local, 256) || !BuildSnapshotFromFolder(&Local, ClientRootFolder)) {
        closesocket(Socket);
        WSACleanup();
        return false;
    }

    char *LocalJson = SnapshotToJson(&Local);
    if (LocalJson == NULL || !SendJson(Socket, LocalJson)) {
        free(LocalJson);
        FreeSnapshot(&Local);
        closesocket(Socket);
        WSACleanup();
        return false;
    }

    char *RemoteJson = ReceiveJson(Socket);
    if (RemoteJson == NULL) {
        free(LocalJson);
        FreeSnapshot(&Local);
        closesocket(Socket);
        WSACleanup();
        return false;
    }

    struct snapshot Remote;
    if (!InitSnapshot(&Remote, 256) || !ParseSnapshotJson(&Remote, RemoteJson)) {
        free(RemoteJson);
        free(LocalJson);
        FreeSnapshot(&Local);
        closesocket(Socket);
        WSACleanup();
        return false;
    }

    struct ChangeSet *Changes = CreateChangeSet();
    bool Ok = (Changes != NULL);
    if (Ok) {
        CompareSnapshots(&Local, &Remote, Changes);

        printf("Planned actions: %d\n", Changes->Count);
        for (int i = 0; i < Changes->Count && Ok; i++) {
            const struct FileChange *Change = &Changes->Changes[i];
            const char *Path = (Change->File != NULL) ? Change->File->RelativeFilePath : "";

            if (Change->Type == CHANGE_COPY_TO_LOCAL || Change->Type == CHANGE_REMOTE_NEWER) {
                Ok = DownloadFromServer(Socket, ClientRootFolder, Path);
                printf("  [%s] %s\n", Ok ? "APPLIED_DOWNLOAD" : "DOWNLOAD_FAILED", Path);
                continue;
            }

            if (Change->Type == CHANGE_COPY_TO_REMOTE || Change->Type == CHANGE_LOCAL_NEWER) {
                Ok = UploadToServer(Socket, ClientRootFolder, Path);
                printf("  [%s] %s\n", Ok ? "APPLIED_UPLOAD" : "UPLOAD_FAILED", Path);
                continue;
            }

            if (Change->Type == CHANGE_CONFLICT) {
                printf("  [CONFLICT_SKIPPED] %s\n", Path);
            }
        }
    }

    char *DoneJson = BuildCommandJson("DONE", NULL, -1);
    if (DoneJson != NULL) {
        SendJson(Socket, DoneJson);
        free(DoneJson);
    }

    FreeChangeSet(Changes);
    FreeSnapshot(&Remote);
    free(RemoteJson);
    free(LocalJson);
    FreeSnapshot(&Local);
    closesocket(Socket);
    WSACleanup();
    return Ok;
}
