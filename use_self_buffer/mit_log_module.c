//
//  mit_log_module.c
//
//  Created by gtliu on 7/3/13.
//  Copyright (c) 2013 GT. All rights reserved.
//

#define MIT_derrprintf(fmt, args...) printf("%s %d ERROR:%s: "fmt"\n", __func__, __LINE__, strerror(errno), ##args)

/* logmodule debug switch macro */
/*
 * If define the marco message of log module will be printed into stdout.
 * It can be used when you want to debug the log module.
 *
 * Without the definition you will see nothing.
 * It can be used when you want to relase the log module.
 */
//#define MIT_DEBUG

#ifdef  MIT_DEBUG
#define MIT_dputs(str) printf("%s %d: %s\n", __func__, __LINE__, str)
#define MIT_dprintf(fmt, args...) printf("%s %d: "fmt"\n", __func__, __LINE__, ##args)
#define MITLogEnter  printf("%s:%d %s\n", __func__, __LINE__, "Enter -->");
#define MITLogExit   printf("%s:%d %s\n", __func__, __LINE__, "<--Exist");
#else
#define MIT_dputs(str)
#define MIT_dprintf(fmt, args...)
#define MITLogEnter
#define MITLogExit
#endif

#include "mit_log_module.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>
#include <fnmatch.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>

static int mit_log_opened_flag;

#if MITLOG_DEBUG_ENABLE
static char const *MITLogLevelHeads[]  = {"[COMMON]", "[WARNING]", "[ERROR]"};

#else
static char const *MITLogFileSuffix[]  = {".comm", ".warn", ".err"};
static int MITLogFileMaxNum[] = {MITLOG_MAX_COMM_FILE_NUM, MITLOG_MAX_WARN_FILE_NUM,\
				 MITLOG_MAX_ERROR_FILE_NUM};

static long long MITLogBufferMaxSize[] = {MITLOG_MAX_COMM_BUFFER_SIZE,\
				 MITLOG_MAX_WARN_BUFFER_SIZE, MITLOG_MAX_ERROR_BUFFER_SIZE};

static char applicationName[MITLOG_MAX_APP_NAME_LEN];
static char appLogFilePath[MITLOG_MAX_FILE_NAME_PATH_LEN];
static char *logFilePaths[MITLOG_FILE_INDEX_NUM];
static char *logFileBuffer[MITLOG_FILE_INDEX_NUM];

static pthread_rwlock_t bufferRWlock;     // use for the logFileBuffer

#endif
static FILE *originFilePointers[MITLOG_FILE_INDEX_NUM];


/************************** Inner Tools Function ********************************/
#ifndef MITLOG_DEBUG_ENABLE
// return the log file path, exp: /data/logs/TestApp.comm
static inline void MITGetLogFilePathPrefix(MITLogFileIndex aryIndex, char *chrAry)
{
    const char *fileSuffix = MITLogFileSuffix[aryIndex];
    sprintf(chrAry, "%s%s%s", appLogFilePath, applicationName, fileSuffix);
}

// get origin file size and next should be updated log file's name
MITLogFuncRetValue MITGetLogInfoByIndex(MITLogFileIndex aryIndex, char *nextStoreFile, long long *fileSize)
{
    const char *fileSuffix = MITLogFileSuffix[aryIndex];

    // pattern:  TestApp.comm.*
    char logNumPattern[MITLOG_MAX_LOG_FILE_LEN] = {0};
    sprintf(logNumPattern, "%s%s%s", applicationName, fileSuffix, ".*");
    // origin log file name
    char originFileName[MITLOG_MAX_LOG_FILE_LEN] = {0};
    sprintf(originFileName, "%s%s", applicationName, fileSuffix);

    DIR *dirfd = NULL;
    struct dirent *entry = NULL;

    if ((dirfd = opendir(appLogFilePath)) == NULL) {
        MIT_derrprintf("opendir() faild");
        return MITLOG_RETV_FAIL;
    }
    // 1 mean the '.' between num and suffix. TestApp.comm.1
    long long partLen = strlen(applicationName) + strlen(fileSuffix) + 1;
    int currentMaxFileNum = 0;
    time_t minModifyTime = 0;
    while ((entry = readdir(dirfd)) != NULL) {
        char *dname = entry->d_name;
        if (strcmp(dname, ".") == 0 ||
            strcmp(dname, "..") == 0) {
            continue;
        }
        // get origin log file size
        if (0 == strcmp(dname, originFileName)) {
            char logfilepath[MITLOG_MAX_FILE_NAME_PATH_LEN] = {0};
            sprintf(logfilepath, "%s%s", appLogFilePath, originFileName);
            MIT_dputs(logfilepath);
            struct stat tstat;
            if (stat(logfilepath, &tstat) < 0) {
                MIT_derrprintf("stat() %s faild", logfilepath);
                closedir(dirfd);
                return MITLOG_RETV_FAIL;
            } else {
                *fileSize = tstat.st_size;
            }
        } else if (fnmatch(logNumPattern, dname, FNM_PATHNAME|FNM_PERIOD) == 0) {
            // get next store file
            int tt = atoi(&dname[partLen]);
            if (tt > currentMaxFileNum) {
                currentMaxFileNum = tt;
            }
            char logfilepath[MITLOG_MAX_FILE_NAME_PATH_LEN] = {0};
            sprintf(logfilepath, "%s%s", appLogFilePath, dname);
            MIT_dputs(logfilepath);
            struct stat tstat;
            if (stat(logfilepath, &tstat) < 0) {
                MIT_derrprintf("stat() %s faild", logfilepath);
                closedir(dirfd);
                return MITLOG_RETV_FAIL;
            }
            if (minModifyTime == 0 || tstat.st_mtime < minModifyTime) {
                minModifyTime = tstat.st_mtime;
                memset(nextStoreFile, 0, MITLOG_MAX_FILE_NAME_PATH_LEN);
                strncpy(nextStoreFile, logfilepath, MITLOG_MAX_FILE_NAME_PATH_LEN);
            }
        }
    }
    MIT_dprintf("currentMaxFileNum:%d  nextStoreFile:%s", currentMaxFileNum, nextStoreFile);
    if (currentMaxFileNum < MITLogFileMaxNum[aryIndex]) {
        ++currentMaxFileNum;
        memset(nextStoreFile, 0, MITLOG_MAX_FILE_NAME_PATH_LEN);
        sprintf(nextStoreFile, "%s%s%s.%d", appLogFilePath, applicationName,\
				 MITLogFileSuffix[aryIndex], currentMaxFileNum);
    }
    MIT_dprintf("currentMaxFileNum:%d  nextStoreFile:%s", currentMaxFileNum, nextStoreFile);

    closedir(dirfd);

    return MITLOG_RETV_SUCCESS;
}

// use to copy file
void MITLogCopyFile(FILE *fromFP, FILE *toFP)
{
    char buf[1024*10];
    long long nread;
    // return to the start of the origin file
    fseek(fromFP, 0L, SEEK_SET);

    while ((nread=fread(buf, sizeof(char), 1024*10, fromFP))) {
        char *outp = buf;
        long long nwritten;
        do {
            nwritten = fwrite(outp, sizeof(char), nread, toFP);
            if (nwritten >= 0) {
                nread -= nwritten;
                outp += nwritten;
            } else if(errno != EINTR){
                break;
            }
        } while (nread > 0);
    }
}

// write buffer content into next store file
void MITLogWriteFile(MITLogFileIndex aryIndex, char *msgStr, long long msgSize)
{
    if (msgSize == 0) {
        return;
    }
    // 1. get log module info for special aryIndex
    char nextStoreFile[MITLOG_MAX_FILE_NAME_PATH_LEN] = {0};
    long long originFileSize  = 0;
    MITGetLogInfoByIndex(aryIndex, nextStoreFile, &originFileSize);
    long long fileLeftSize = MITLOG_MAX_FILE_SIZE - originFileSize;
    MIT_dprintf("fileLeftSize:%lld  originFileWroteSize:%lld  msgSize:%lld", fileLeftSize,\
			 originFileSize, msgSize);

    // 2. check whether the origin file has enough space
    if (fileLeftSize < msgSize) {
        MIT_dputs("Oringin file will be Written into next stroe file");
        // cp origin file(ex:TestApp.comm) to next stroe log file(ex:TestApp.comm)
        FILE *nextStoreFP = fopen(nextStoreFile, "w");
        if (nextStoreFP == NULL) {
            MIT_derrprintf("fopen() %s faild", nextStoreFile);
        }
        MITLogCopyFile(originFilePointers[aryIndex], nextStoreFP);
        if (fflush(nextStoreFP) !=0 ) {
            MIT_derrprintf("fflush() failed: %s", nextStoreFile);
        }
        fclose(nextStoreFP);
        // empty the origin file this step MUST be executed.
        int ret = ftruncate(fileno(originFilePointers[aryIndex]), 0);
        if (ret == -1) {
            MIT_derrprintf("ftruncate() failed:");
        }
    }

    // 3. write buffer into origin file
    char *outp = msgStr;
    long long num = msgSize;
    long long nwritten;
    do {
        nwritten = fwrite(outp, sizeof(char), num, originFilePointers[aryIndex]);
        if (nwritten >= 0) {
            num -= nwritten;
            outp += nwritten;
        } else if(errno != EINTR){
            break;
        }
    } while (num > 0);
    if (fflush(originFilePointers[aryIndex]) !=0 ) {
        MIT_derrprintf("fflush() failed: %d", aryIndex);
    }
}
#endif
// get the right aryIndex for special log level
static inline MITLogFileIndex MITGetIndexForLevel(MITLogLevel level)
{
    switch (level) {
        case MITLOG_LEVEL_COMMON:
            return MITLOG_INDEX_COMM_FILE;
            break;
        case MITLOG_LEVEL_WARNING:
            return MITLOG_INDEX_WARN_FILE;
            break;
        case MITLOG_LEVEL_ERROR:
            return MITLOG_INDEX_ERROR_FILE;
            break;
        default:
            break;
    }
    return MITLOG_INDEX_COMM_FILE;
}

/************************** MITLog Module Function ********************************/
MITLogFuncRetValue MITLogOpen(const char *appName, const char*logPath)
{
    /* this use to avoid double times to open log module */
    if (mit_log_opened_flag == 1) {
        return MITLOG_RETV_HAS_OPENED;
    }
    mit_log_opened_flag = 1;

#if MITLOG_DEBUG_ENABLE
    originFilePointers[MITLOG_INDEX_COMM_FILE] = stdout;
    originFilePointers[MITLOG_INDEX_WARN_FILE] = stderr;
    originFilePointers[MITLOG_INDEX_ERROR_FILE] = stderr;
    return MITLOG_RETV_SUCCESS;
#else
    size_t pathLen = strlen(logPath);
    int maxPath = MITLOG_MAX_FILE_NAME_PATH_LEN - MITLOG_MAX_APP_NAME_LEN - 2;
    if (strlen(appName) == 0 || pathLen == 0 || pathLen > maxPath) {
        MIT_dprintf("ERROR: %s %d", "appName can't be empty; \
                    pathLen can't be empty and legth litter than", maxPath);
        return MITLOG_RETV_PARAM_ERROR;
    }
    memset(applicationName, 0, sizeof(applicationName));
    memset(appLogFilePath, 0, sizeof(appLogFilePath));
    strncpy(applicationName, appName, MITLOG_MAX_APP_NAME_LEN);
    strncpy(appLogFilePath, logPath, pathLen);
    if (logPath[pathLen-1] != '/') {
        appLogFilePath[pathLen] = '/';
    }
    // init the read/write lock
    int ret = pthread_rwlock_init(&bufferRWlock, NULL);
    if (ret != 0) {
        MIT_derrprintf("pthread_rwlock_init() failed:%d", ret);
        return MITLOG_RETV_FAIL;
    }
    // keep the log path exist
    ret = mkdir(appLogFilePath, S_IRWXU|S_IRWXG|S_IRWXO);
    if (ret == -1 && errno != EEXIST) {
        MIT_derrprintf("mkdir() failed:%d", ret);
        return MITLOG_RETV_FAIL;
    }
    // alloc memory
    for (int i = MITLOG_INDEX_COMM_FILE; i<= MITLOG_INDEX_ERROR_FILE; ++i) {
        logFilePaths[i] = (char *)calloc(MITLOG_MAX_FILE_NAME_PATH_LEN, sizeof(char));
        if (!logFilePaths[i]) {
            for (int j=MITLOG_INDEX_COMM_FILE; j<i; ++j) {
                free(logFilePaths[j]);
                logFilePaths[j] = NULL;
            }
            MIT_derrprintf("Allocate memroy Faild");
            return MITLOG_RETV_ALLOC_MEM_FAIL;
        }
        logFileBuffer[i] = (char *)calloc(MITLogBufferMaxSize[i], sizeof(char));
        if (!logFileBuffer[i]) {
            for (int j=MITLOG_INDEX_COMM_FILE; j<MITLOG_INDEX_ERROR_FILE; ++j) {
                free(logFilePaths[j]);
                logFilePaths[j] = NULL;
            }
            for (int j=MITLOG_INDEX_COMM_FILE; j<i; ++j) {
                free(logFileBuffer[j]);
                logFileBuffer[j] = NULL;
            }
            MIT_derrprintf("Allocate memroy Faild");
            return MITLOG_RETV_ALLOC_MEM_FAIL;
        }
    }
    // open files
    for (int i = MITLOG_INDEX_COMM_FILE; i<= MITLOG_INDEX_ERROR_FILE; ++i) {
        sprintf(logFilePaths[i], "%s%s%s", appLogFilePath, appName, MITLogFileSuffix[i]);
        originFilePointers[i] = fopen(logFilePaths[i], "a+");
        if (!originFilePointers[i]) {
            for (int j=MITLOG_INDEX_COMM_FILE; j<=MITLOG_INDEX_ERROR_FILE; ++j) {
                free(logFilePaths[j]);
                logFilePaths[j] = NULL;
                free(logFileBuffer[j]);
                logFileBuffer[j] = NULL;
            }
            for (int j=MITLOG_INDEX_COMM_FILE; j<i; ++j) {
                fclose(originFilePointers[j]);
                originFilePointers[j] = NULL;
            }
            MIT_derrprintf("Open %s Faild", logFilePaths[i]);
            return MITLOG_RETV_OPEN_FILE_FAIL;
        }
    }

    return MITLOG_RETV_SUCCESS;
#endif
}

MITLogFuncRetValue MITLogWrite(MITLogLevel level, const char *fmt, ...)
{
    // 1. generate the string
    int n, size = 100;      // suppose the message need no more than 100 bytes
    char *tarp = NULL, *np = NULL;
    va_list ap;
    if ((tarp = malloc(size)) == NULL) {
        return MITLOG_RETV_ALLOC_MEM_FAIL;
    }
    while (1) {
        // try to print the allocated space
        va_start(ap, fmt);
        n = vsnprintf(tarp, size, fmt, ap);
        va_end(ap);
        if (n > -1 && n < size) {
            break;
        }
        // try more space
        if (n > -1) {      // glibc 2.1
            size = n + 1;  // 1 is for the '\0'
        } else {           // glibc 2.0
            size = n * 2;  // twice the old size
        }
        if ((np = realloc(tarp, size)) == NULL) {
            free(tarp);
            return MITLOG_RETV_ALLOC_MEM_FAIL;
        } else {
            tarp = np;
        }
    }
    // 2. add prefix and suffix to the message
    time_t now = time(NULL);
    char timeStr[48] = {0};
    snprintf(timeStr, 48, "[%ld]", now);
    long long sumLen = strlen(timeStr) + 1 + strlen(tarp) \
			+ strlen("\n") + 5;   // 5 keep enough space and last space is '\0'
    char *tarMessage = (char *)calloc(sumLen, sizeof(char));
    if (!tarMessage) {
        free(tarp);
        tarp = NULL;
        return MITLOG_RETV_ALLOC_MEM_FAIL;
    }
    sprintf(tarMessage, "%s %s\n", timeStr, tarp);
    sumLen = strlen(tarMessage);

    free(tarp);
    np = tarp = NULL;
    //MIT_dprintf("target message:%s", tarMessage);
    MITLogFileIndex aryIndex = MITGetIndexForLevel(level);

#if MITLOG_DEBUG_ENABLE
    fprintf(originFilePointers[aryIndex], "%-10s %s", MITLogLevelHeads[aryIndex], tarMessage);
#else
    // 3. add target message into buffer or file
    pthread_rwlock_wrlock(&bufferRWlock);
    long long bufferUesedSize = strlen(logFileBuffer[aryIndex]);
    if (MITLogBufferMaxSize[aryIndex] - bufferUesedSize > sumLen) {
        // write into buffer
        strcat(logFileBuffer[aryIndex], tarMessage);
        pthread_rwlock_unlock(&bufferRWlock);
    } else {
        MIT_dputs("String write into file*****************");
        pthread_rwlock_unlock(&bufferRWlock);

        pthread_rwlock_rdlock(&bufferRWlock);
        // 1. write the buffer into file
        long long firstLen = strlen(logFileBuffer[aryIndex]);
        MITLogWriteFile(aryIndex, logFileBuffer[aryIndex], firstLen);
        pthread_rwlock_unlock(&bufferRWlock);

        pthread_rwlock_wrlock(&bufferRWlock);
        // empty the buffer
        memset(logFileBuffer[aryIndex], 0, MITLogBufferMaxSize[aryIndex]);
        // 2. write into origin file or buffer
        if (sumLen < MITLogBufferMaxSize[aryIndex]) {
            // write into buffer
            strcat(logFileBuffer[aryIndex], tarMessage);
            pthread_rwlock_unlock(&bufferRWlock);
        } else {
            pthread_rwlock_unlock(&bufferRWlock);
            // write into origin file directly
            MITLogWriteFile(aryIndex, tarMessage, sumLen);
        }
    }

    free(tarMessage);
    tarMessage = NULL;
#endif
    return MITLOG_RETV_SUCCESS;
}

void MITLogFlush(void)
{
#ifndef MITLOG_DEBUG_ENABLE
    for (int i=MITLOG_INDEX_COMM_FILE; i<=MITLOG_INDEX_ERROR_FILE; ++i) {
        MITLogWriteFile(i, logFileBuffer[i], strlen(logFileBuffer[i]));
        fflush(originFilePointers[i]);
    }
#endif
}

void MITLogClose(void)
{
    if (mit_log_opened_flag == 0) {
        return;
    }
#ifndef MITLOG_DEBUG_ENABLE
    // 1. flush the buffers
    MITLogFlush();
    // 2. release the resources
    for (int j=MITLOG_INDEX_COMM_FILE; j<=MITLOG_INDEX_ERROR_FILE; ++j) {
        fclose(originFilePointers[j]);
        originFilePointers[j] = NULL;

        free(logFilePaths[j]);
        logFilePaths[j] = NULL;
        free(logFileBuffer[j]);
        logFileBuffer[j] = NULL;
    }
    // 3. destroy the rwlock
    int ret = pthread_rwlock_destroy(&bufferRWlock);
    if (ret != 0) {
        MIT_dprintf("pthread_rwlock_destroy() failed:%d", ret);
    }
#endif
}






















