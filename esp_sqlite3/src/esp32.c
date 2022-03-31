/* From: https://chromium.googlesource.com/chromium/src.git/+/4.1.249.1050/third_party/sqlite/src/os_symbian.cc
 * https://github.com/spsoft/spmemvfs/tree/master/spmemvfs
 * http://www.sqlite.org/src/doc/trunk/src/test_demovfs.c
 * http://www.sqlite.org/src/doc/trunk/src/test_vfstrace.c
 * http://www.sqlite.org/src/doc/trunk/src/test_onefile.c
 * http://www.sqlite.org/src/doc/trunk/src/test_vfs.c
 * https://github.com/nodemcu/nodemcu-firmware/blob/master/app/sqlite3/esp8266.c
 **/

#include <esp_system.h>
#include <rom/ets_sys.h>

#include "sqlite3.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*  SPI Flash sector size (SPI_FLASH_SEC_SIZE) from <esp_spi_flash.h> */
#define SECTOR_SIZE   4096

#define CACHEBLOCKSZ  64
#define MAX_NAME_SIZE 100

#ifndef USE_SHOX96
#define USE_SHOX96 (0)
#endif

#if USE_SHOX96
#include "shox96_0_2.h"
#endif

#undef dbg_printf
//#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_printf(...)

typedef struct linkedlist_s {
    uint16_t blockid;
    struct linkedlist_s* next;
    uint8_t data[CACHEBLOCKSZ];
} linkedlist_t;

typedef struct filecache_s {
    uint32_t size;
    linkedlist_t* list;
} filecache_t;

typedef struct vfsFile_s {
    sqlite3_file base;
    FILE* fd;
    filecache_t* cache;
    char name[MAX_NAME_SIZE];
} vfsFile_t;

static int vfsClose(sqlite3_file*);
static int vfsLock(sqlite3_file*, int);
static int vfsUnlock(sqlite3_file*, int);
static int vfsSync(sqlite3_file*, int);
static int vfsOpen(sqlite3_vfs*, const char*, sqlite3_file*, int, int*);
static int vfsRead(sqlite3_file*, void*, int, sqlite3_int64);
static int vfsWrite(sqlite3_file*, const void*, int, sqlite3_int64);
static int vfsTruncate(sqlite3_file*, sqlite3_int64);
static int vfsDelete(sqlite3_vfs*, const char*, int);
static int vfsFileSize(sqlite3_file*, sqlite3_int64*);
static int vfsAccess(sqlite3_vfs*, const char*, int, int*);
static int vfsFullPathname(sqlite3_vfs*, const char*, int, char*);
static int vfsCheckReservedLock(sqlite3_file*, int*);
static int vfsFileControl(sqlite3_file*, int, void*);
static int vfsSectorSize(sqlite3_file*);
static int vfsDeviceCharacteristics(sqlite3_file*);
static int vfsRandomness(sqlite3_vfs*, int, char*);
static int vfsSleep(sqlite3_vfs*, int);
static int vfsCurrentTime(sqlite3_vfs*, double*);
static int vfsMemClose(sqlite3_file*);
static int vfsMemRead(sqlite3_file*, void*, int, sqlite3_int64);
static int vfsMemWrite(sqlite3_file*, const void*, int, sqlite3_int64);
static int vfsMemFileSize(sqlite3_file*, sqlite3_int64*);
static int vfsMemSync(sqlite3_file*, int);
static void* vfsDlOpen(sqlite3_vfs*, const char*);
static void vfsDlError(sqlite3_vfs*, int, char*);
static void vfsDlClose(sqlite3_vfs*, void*);
void (*vfsDlSym(sqlite3_vfs*, void*, const char*))(void);

static sqlite3_vfs esp32Vfs = {
    .iVersion = 1, /* Structure version number (maximum 3)  */
    .szOsFile = sizeof(vfsFile_t), /* Size of subclassed sqlite3_file */
    .mxPathname = (1 + (MAX_NAME_SIZE)), /* Maximum file pathname length */
    .pNext = NULL, /* Next registered VFS */
    .zName = "esp32", /* Name of this virtual file system */
    .pAppData = NULL, /* Pointer to application-specific data */
    .xOpen = vfsOpen,
    .xDelete = vfsDelete,
    .xAccess = vfsAccess,
    .xFullPathname = vfsFullPathname,
    .xDlOpen = vfsDlOpen,
    .xDlError = vfsDlError,
    .xDlSym = vfsDlSym,
    .xDlClose = vfsDlClose,
    .xRandomness = vfsRandomness,
    .xSleep = vfsSleep,
    .xCurrentTime = vfsCurrentTime,
    .xGetLastError = NULL,
    /*
    ** The methods above are in version 1 of the sqlite_vfs object
    ** definition.  Those that follow are added in version 2 or later
    */
    .xCurrentTimeInt64 = NULL,
    /*
    ** The methods above are in versions 1 and 2 of the sqlite_vfs object.
    ** Those below are for version 3 and greater.
    */
    .xSetSystemCall = NULL,
    .xGetSystemCall = NULL,
    .xNextSystemCall = NULL,
    /*
    ** The methods above are in versions 1 through 3 of the sqlite_vfs object.
    ** New fields may be appended in future versions.  The iVersion
    ** value will increment whenever this happens.
    */
};

static sqlite3_io_methods esp32IoMethods = {
    .iVersion = 1,
    .xClose = vfsClose,
    .xRead = vfsRead,
    .xWrite = vfsWrite,
    .xTruncate = vfsTruncate,
    .xSync = vfsSync,
    .xFileSize = vfsFileSize,
    .xLock = vfsLock,
    .xUnlock = vfsUnlock,
    .xCheckReservedLock = vfsCheckReservedLock,
    .xFileControl = vfsFileControl,
    .xSectorSize = vfsSectorSize,
    .xDeviceCharacteristics = vfsDeviceCharacteristics,
    /* Methods above are valid for version 1 */
    .xShmMap = NULL,
    .xShmLock = NULL,
    .xShmBarrier = NULL,
    .xShmUnmap = NULL,
    /* Methods above are valid for version 2 */
    .xFetch = NULL,
    .xUnfetch = NULL,
    /* Methods above are valid for version 3 */
    /* Additional methods may be added in future releases */
};

static sqlite3_io_methods esp32MemMethods = {
    .iVersion = 1,
    .xClose = vfsMemClose,
    .xRead = vfsMemRead,
    .xWrite = vfsMemWrite,
    .xTruncate = vfsTruncate,
    .xSync = vfsMemSync,
    .xFileSize = vfsMemFileSize,
    .xLock = vfsLock,
    .xUnlock = vfsUnlock,
    .xCheckReservedLock = vfsCheckReservedLock,
    .xFileControl = vfsFileControl,
    .xSectorSize = vfsSectorSize,
    .xDeviceCharacteristics = vfsDeviceCharacteristics,
    /* Methods above are valid for version 1 */
    .xShmMap = NULL,
    .xShmLock = NULL,
    .xShmBarrier = NULL,
    .xShmUnmap = NULL,
    /* Methods above are valid for version 2 */
    .xFetch = NULL,
    .xUnfetch = NULL,
    /* Methods above are valid for version 3 */
    /* Additional methods may be added in future releases */
};

static uint32_t linkedlist_store(linkedlist_t** leaf, uint32_t offset, uint32_t len, const uint8_t* data)
{
    const uint8_t blank[CACHEBLOCKSZ] = { 0 };
    uint16_t blockid = offset / CACHEBLOCKSZ;
    linkedlist_t* block;

    if (!memcmp(data, blank, CACHEBLOCKSZ))
        return len;

    block = *leaf;
    if (!block || (block->blockid != blockid)) {
        block = (linkedlist_t*)sqlite3_malloc(sizeof(linkedlist_t));
        if (!block)
            return SQLITE_NOMEM;

        memset(block->data, 0, CACHEBLOCKSZ);
        block->blockid = blockid;
    }

    if (!*leaf) {
        *leaf = block;
        block->next = NULL;
    } else if (block != *leaf) {
        if (block->blockid > (*leaf)->blockid) {
            block->next = (*leaf)->next;
            (*leaf)->next = block;
        } else {
            block->next = (*leaf);
            (*leaf) = block;
        }
    }

    memcpy(block->data + offset % CACHEBLOCKSZ, data, len);

    return len;
}

static uint32_t filecache_pull(filecache_t* cache, uint32_t offset, uint32_t len, uint8_t* data)
{
    uint16_t i;
    float blocks;
    uint32_t r = 0;

    blocks = (offset % CACHEBLOCKSZ + len) / (float)CACHEBLOCKSZ;
    if (blocks == 0.0)
        return 0;
    if (!cache->list)
        return 0;

    if ((blocks - (int)blocks) > 0.0)
        blocks = blocks + 1.0;

    for (i = 0; i < (uint16_t)blocks; i++) {
        uint16_t round;
        float relablock;
        linkedlist_t* leaf;
        uint32_t relaoffset, relalen;
        uint8_t* reladata = (uint8_t*)data;

        relalen = len - r;

        reladata = reladata + r;
        relaoffset = offset + r;

        round = CACHEBLOCKSZ - relaoffset % CACHEBLOCKSZ;
        if (relalen > round)
            relalen = round;

        for (leaf = cache->list; leaf && leaf->next; leaf = leaf->next) {
            if ((leaf->next->blockid * CACHEBLOCKSZ) > relaoffset)
                break;
        }

        relablock = relaoffset / ((float)CACHEBLOCKSZ) - leaf->blockid;

        if ((relablock >= 0) && (relablock < 1))
            memcpy(data + r, leaf->data + (relaoffset % CACHEBLOCKSZ), relalen);

        r = r + relalen;
    }

    return 0;
}

static uint32_t filecache_push(filecache_t* cache, uint32_t offset, uint32_t len, const uint8_t* data)
{
    uint16_t i;
    float blocks;
    uint32_t r = 0;
    uint8_t updateroot = 0x1;

    blocks = (offset % CACHEBLOCKSZ + len) / (float)CACHEBLOCKSZ;

    if (blocks == 0.0)
        return 0;

    if ((blocks - (int)blocks) > 0.0)
        blocks = blocks + 1.0;

    for (i = 0; i < (uint16_t)blocks; i++) {
        uint16_t round;
        uint32_t localr;
        linkedlist_t* leaf;
        uint32_t relaoffset, relalen;
        uint8_t* reladata = (uint8_t*)data;

        relalen = len - r;

        reladata = reladata + r;
        relaoffset = offset + r;

        round = CACHEBLOCKSZ - relaoffset % CACHEBLOCKSZ;
        if (relalen > round)
            relalen = round;

        for (leaf = cache->list; leaf && leaf->next; leaf = leaf->next) {
            if ((leaf->next->blockid * CACHEBLOCKSZ) > relaoffset)
                break;
            updateroot = 0x0;
        }

        localr = linkedlist_store(&leaf, relaoffset, (relalen > CACHEBLOCKSZ) ? CACHEBLOCKSZ : relalen, reladata);
        if (localr == SQLITE_NOMEM)
            return SQLITE_NOMEM;

        r = r + localr;

        if (updateroot & 0x1)
            cache->list = leaf;
    }

    if (offset + len > cache->size)
        cache->size = offset + len;

    return r;
}

static void filecache_free(filecache_t* cache)
{
    linkedlist_t* next = NULL;
    linkedlist_t* ll = cache->list;
    while (ll != NULL) {
        next = ll->next;
        sqlite3_free(ll);
        ll = next;
    }
}

static int vfsAccess(sqlite3_vfs* vfs, const char* path, int flags, int* result)
{
    struct stat st;
    memset(&st, 0, sizeof(struct stat));
    int rc = stat(path, &st);
    *result = (rc != -1);

    dbg_printf("vfsAccess: %s %d %d %ld\n", path, *result, rc, st.st_size);
    return SQLITE_OK;
}

static int vfsMemClose(sqlite3_file* id)
{
    vfsFile_t* file = (vfsFile_t*)id;

    filecache_free(file->cache);
    sqlite3_free(file->cache);

    dbg_printf("vfsMemClose: %s OK\n", file->name);
    return SQLITE_OK;
}

static int vfsMemRead(sqlite3_file* id, void* buffer, int amount, sqlite3_int64 offset)
{
    int32_t ofst;
    vfsFile_t* file = (vfsFile_t*)id;
    ofst = (int32_t)(offset & 0x7FFFFFFF);

    filecache_pull(file->cache, ofst, amount, (uint8_t*)buffer);

    dbg_printf("vfsMemRead: %s [%d] [%d] OK\n", file->name, ofst, amount);
    return SQLITE_OK;
}

static int vfsMemWrite(sqlite3_file* id, const void* buffer, int amount, sqlite3_int64 offset)
{
    int32_t ofst;
    vfsFile_t* file = (vfsFile_t*)id;

    ofst = (int32_t)(offset & 0x7FFFFFFF);

    filecache_push(file->cache, ofst, amount, (const uint8_t*)buffer);

    dbg_printf("vfsMemWrite: %s [%d] [%d] OK\n", file->name, ofst, amount);
    return SQLITE_OK;
}

static int vfsMemSync(sqlite3_file* id, int flags)
{
    dbg_printf("vfsMemSync: %s OK\n", ((vfsFile_t*)id)->name);
    return SQLITE_OK;
}

static int vfsMemFileSize(sqlite3_file* id, sqlite3_int64* size)
{
    vfsFile_t* file = (vfsFile_t*)id;

    *size = 0LL | file->cache->size;
    dbg_printf("vfsMemFileSize: %s [%d] OK\n", file->name, file->cache->size);
    return SQLITE_OK;
}

static int vfsOpen(sqlite3_vfs* vfs, const char* path, sqlite3_file* file, int flags, int* outflags)
{
    // int rc;
    char mode[5];
    vfsFile_t* p = (vfsFile_t*)file;

    strcpy(mode, "r");
    if (path == NULL)
        return SQLITE_IOERR;
    dbg_printf("vfsOpen: 0o %s %s\n", path, mode);
    if (flags & SQLITE_OPEN_READONLY)
        strcpy(mode, "r");
    if (flags & SQLITE_OPEN_READWRITE || flags & SQLITE_OPEN_MAIN_JOURNAL) {
        int result;
        if (SQLITE_OK != vfsAccess(vfs, path, flags, &result))
            return SQLITE_CANTOPEN;

        if (result == 1)
            strcpy(mode, "r+");
        else
            strcpy(mode, "w+");
    }

    dbg_printf("vfsOpen: 1o %s %s\n", path, mode);
    memset(p, 0, sizeof(vfsFile_t));

    strncpy(p->name, path, MAX_NAME_SIZE);
    p->name[MAX_NAME_SIZE - 1] = '\0';

    if (flags & SQLITE_OPEN_MAIN_JOURNAL) {
        p->fd = 0;
        p->cache = (filecache_t*)sqlite3_malloc(sizeof(filecache_t));
        if (!p->cache)
            return SQLITE_NOMEM;
        memset(p->cache, 0, sizeof(filecache_t));

        p->base.pMethods = &esp32MemMethods;
        dbg_printf("vfsOpen: 2o %s MEM OK\n", p->name);
        return SQLITE_OK;
    }

    p->fd = fopen(path, mode);
    if (p->fd == NULL) {
        return SQLITE_CANTOPEN;
    }

    p->base.pMethods = &esp32IoMethods;
    dbg_printf("vfsOpen: 2o %s OK\n", p->name);
    return SQLITE_OK;
}

static int vfsClose(sqlite3_file* id)
{
    vfsFile_t* file = (vfsFile_t*)id;

    int rc = fclose(file->fd);
    dbg_printf("vfsClose: %s %d\n", file->name, rc);
    return rc ? SQLITE_IOERR_CLOSE : SQLITE_OK;
}

static int vfsRead(sqlite3_file* id, void* buffer, int amount, sqlite3_int64 offset)
{
    size_t nRead;
    int32_t ofst, iofst;
    vfsFile_t* file = (vfsFile_t*)id;

    iofst = (int32_t)(offset & 0x7FFFFFFF);

    dbg_printf("vfsRead: 1r %s %d %lld[%d] \n", file->name, amount, offset, iofst);
    ofst = fseek(file->fd, iofst, SEEK_SET);
    if (ofst != 0) {
        dbg_printf("vfsRead: 2r %d != %d FAIL\n", ofst, iofst);
        return SQLITE_IOERR_SHORT_READ /* SQLITE_IOERR_SEEK */;
    }

    nRead = fread(buffer, 1, amount, file->fd);
    if (nRead > amount) {
        dbg_printf("vfsRead: 3r %s FAIL\n", file->name);
        return SQLITE_IOERR_READ;
    } else if (nRead < amount) {
        dbg_printf("vfsRead: 3r %s %u %d FAIL\n", file->name, nRead, amount);
        return SQLITE_IOERR_SHORT_READ;
    }

    dbg_printf("vfsRead: 3r %s %u %d OK\n", file->name, nRead, amount);
    return SQLITE_OK;
}

static int vfsWrite(sqlite3_file* id, const void* buffer, int amount, sqlite3_int64 offset)
{
    size_t nWrite;
    int32_t ofst, iofst;
    vfsFile_t* file = (vfsFile_t*)id;

    iofst = (int32_t)(offset & 0x7FFFFFFF);

    dbg_printf("vfsWrite: 1w %s %d %lld[%d] \n", file->name, amount, offset, iofst);
    ofst = fseek(file->fd, iofst, SEEK_SET);
    if (ofst != 0) {
        return SQLITE_IOERR_SEEK;
    }

    nWrite = fwrite(buffer, 1, amount, file->fd);
    if (nWrite != amount) {
        dbg_printf("vfsWrite: 2w %s %u %d\n", file->name, nWrite, amount);
        return SQLITE_IOERR_WRITE;
    }

    dbg_printf("vfsWrite: 3w %s OK\n", file->name);
    return SQLITE_OK;
}

static int vfsTruncate(sqlite3_file* id, sqlite3_int64 bytes)
{
    return SQLITE_OK;
}

static int vfsDelete(sqlite3_vfs* vfs, const char* path, int syncDir)
{
    int32_t rc = remove(path);
    if (rc)
        return SQLITE_IOERR_DELETE;

    dbg_printf("vfsDelete: %s OK\n", path);
    return SQLITE_OK;
}

static int vfsFileSize(sqlite3_file* id, sqlite3_int64* size)
{
    vfsFile_t* file = (vfsFile_t*)id;
    dbg_printf("vfsFileSize: %s: ", file->name);
    struct stat st;
    int fno = fileno(file->fd);
    if (fno == -1)
        return SQLITE_IOERR_FSTAT;
    if (fstat(fno, &st))
        return SQLITE_IOERR_FSTAT;
    *size = st.st_size;
    dbg_printf(" %ld[%lld]\n", st.st_size, *size);
    return SQLITE_OK;
}

static int vfsSync(sqlite3_file* id, int flags)
{
    vfsFile_t* file = (vfsFile_t*)id;

    int rc = fflush(file->fd);
    fsync(fileno(file->fd));
    dbg_printf("vfsSync( %s: ): %d \n", file->name, rc);

    return rc ? SQLITE_IOERR_FSYNC : SQLITE_OK;
}

static int vfsFullPathname(sqlite3_vfs* vfs, const char* path, int len, char* fullpath)
{
    // structure stat does not have name.
    // struct stat st;
    // int32_t rc = stat( path, &st );
    // if ( rc == 0 ){
    //	strncpy( fullpath, st.name, len );
    // } else {
    //	strncpy( fullpath, path, len );
    // }

    // As now just copy the path
    strncpy(fullpath, path, len);
    fullpath[len - 1] = '\0';

    dbg_printf("vfsFullPathname: %s\n", fullpath);
    return SQLITE_OK;
}

static int vfsLock(sqlite3_file* id, int lock_type)
{
    return SQLITE_OK;
}

static int vfsUnlock(sqlite3_file* id, int lock_type)
{
    return SQLITE_OK;
}

static int vfsCheckReservedLock(sqlite3_file* id, int* result)
{
    *result = 0;
    return SQLITE_OK;
}

static int vfsFileControl(sqlite3_file* id, int op, void* arg)
{
    return SQLITE_OK;
}

static int vfsSectorSize(sqlite3_file* id)
{
    return SECTOR_SIZE;
}

static int vfsDeviceCharacteristics(sqlite3_file* id)
{
    return 0;
}

void* vfsDlOpen(sqlite3_vfs* vfs, const char* path)
{
    return NULL;
}

void vfsDlError(sqlite3_vfs* vfs, int len, char* errmsg)
{
    return;
}

void (*vfsDlSym(sqlite3_vfs* vfs, void* handle, const char* symbol))(void)
{
    return NULL;
}

void vfsDlClose(sqlite3_vfs* vfs, void* handle)
{
    return;
}

static int vfsRandomness(sqlite3_vfs* vfs, int len, char* buffer)
{
    long rdm;
    int sz = 1 + (len / sizeof(long));
    char a_rdm[sz * sizeof(long)];
    while (sz--) {
        rdm = esp_random();
        memcpy(a_rdm + sz * sizeof(long), &rdm, sizeof(long));
    }
    memcpy(buffer, a_rdm, len);
    return SQLITE_OK;
}

static int vfsSleep(sqlite3_vfs* vfs, int microseconds)
{
    ets_delay_us(microseconds);
    return SQLITE_OK;
}

static int vfsCurrentTime(sqlite3_vfs* vfs, double* result)
{
    time_t t = time(NULL);
    *result = t / 86400.0 + 2440587.5;
    // This is stubbed out until we have a working RTCTIME solution;
    // as it stood, this would always have returned the UNIX epoch.
    //*result = 2440587.5;
    dbg_printf("vfsCurrentTime: %g\n", *result);
    return SQLITE_OK;
}

#if USE_SHOX96

// From https://stackoverflow.com/questions/19758270/read-varint-from-linux-sockets#19760246
// Encode an unsigned 64-bit varint.  Returns number of encoded bytes.
// 'buffer' must have room for up to 10 bytes.
static int encode_unsigned_varint(uint8_t* buffer, uint64_t value)
{
    int encoded = 0;
    do {
        uint8_t next_byte = value & 0x7F;
        value >>= 7;
        if (value)
            next_byte |= 0x80;
        buffer[encoded++] = next_byte;
    } while (value);
    return encoded;
}

static uint64_t decode_unsigned_varint(const uint8_t* data, int* decoded_bytes)
{
    int i = 0;
    uint64_t decoded_value = 0;
    int shift_amount = 0;
    do {
        decoded_value |= (uint64_t)(data[i] & 0x7F) << shift_amount;
        shift_amount += 7;
    } while ((data[i++] & 0x80) != 0);
    *decoded_bytes = i;
    return decoded_value;
}

static void shox96_0_2c(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    int nIn, nOut;
    long int nOut2;
    const unsigned char* inBuf;
    unsigned char* outBuf;
    unsigned char vInt[9];
    int vIntLen;

    assert(argc == 1);
    nIn = sqlite3_value_bytes(argv[0]);
    inBuf = (unsigned char*)sqlite3_value_blob(argv[0]);
    nOut = 13 + nIn + (nIn + 999) / 1000;
    vIntLen = encode_unsigned_varint(vInt, (uint64_t)nIn);

    outBuf = (unsigned char*)malloc(nOut + vIntLen);
    memcpy(outBuf, vInt, vIntLen);
    nOut2 = shox96_0_2_compress((const char*)inBuf, nIn, (char*)&outBuf[vIntLen], NULL);
    sqlite3_result_blob(context, outBuf, nOut2 + vIntLen, free);
}

static void shox96_0_2d(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    unsigned int nIn, nOut, rc;
    const unsigned char* inBuf;
    unsigned char* outBuf;
    long int nOut2;
    uint64_t inBufLen64;
    int vIntLen;

    assert(argc == 1);

    if (sqlite3_value_type(argv[0]) != SQLITE_BLOB)
        return;

    nIn = sqlite3_value_bytes(argv[0]);
    if (nIn < 2) {
        return;
    }
    inBuf = (unsigned char*)sqlite3_value_blob(argv[0]);
    inBufLen64 = decode_unsigned_varint(inBuf, &vIntLen);
    nOut = (unsigned int)inBufLen64;
    outBuf = (unsigned char*)malloc(nOut);
    // nOut2 = (long int)nOut;
    nOut2 = shox96_0_2_decompress((const char*)(inBuf + vIntLen), nIn - vIntLen, (char*)outBuf, NULL);
    // if( rc!=Z_OK ){
    //   free(outBuf);
    // }else{
    sqlite3_result_blob(context, outBuf, nOut2, free);
    //}
}

static int registerShox96_0_2(sqlite3* db, const char** pzErrMsg, const struct sqlite3_api_routines* pThunk)
{
    sqlite3_create_function(db, "shox96_0_2c", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, shox96_0_2c, 0, 0);
    sqlite3_create_function(db, "shox96_0_2d", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, shox96_0_2d, 0, 0);
    return SQLITE_OK;
}

#endif /* USE_SHOX96 */

int sqlite3_os_init(void)
{
    sqlite3_vfs_register(&esp32Vfs, 1);
#if USE_SHOX96
    sqlite3_auto_extension((void (*)())registerShox96_0_2);
#endif
    return SQLITE_OK;
}

int sqlite3_os_end(void)
{
    return SQLITE_OK;
}
