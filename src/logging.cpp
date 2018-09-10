// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "logging.h"
#include "util.h"
#include "utiltime.h"
#include "fs.h"
#include "fs_utils.h"

bool fLogIPs = DEFAULT_LOGIPS;

/**
 * NOTE: the logger instance is leaked on exit. This is ugly, but will be
 * cleaned up by the OS/libc. Defining a logger as a global object doesn't work
 * since the order of destruction of static/global objects is undefined.
 * Consider if the logger gets destroyed, and then some later destructor calls
 * LogPrintf, maybe indirectly, and you get a core dump at shutdown trying to
 * access the logger. When the shutdown sequence is fully audited and tested,
 * explicit destruction of these objects can be implemented by changing this
 * from a raw pointer to a std::unique_ptr.
 *
 * This method of initialization was originally introduced in
 * ee3374234c60aba2cc4c5cd5cac1c0aefc2d817c.
 */
TessaLog::Logger &GetLogger() {
    static TessaLog::Logger *const logger = new TessaLog::Logger();
    return *logger;
}

static int FileWriteStr(const std::string &str, FILE *fp) {
    return fwrite(str.data(), 1, str.size(), fp);
}

void TessaLog::Logger::OpenDebugLog() {
    std::lock_guard<std::mutex> scoped_lock(mutexDebugLog);

    assert(fileout == nullptr);
    fs::path pathDebug = GetDataDir() / "debug.log";
    fileout = fsbridge::fopen(pathDebug, "a");
    if (fileout) {
        // Unbuffered.
        setbuf(fileout, nullptr);
        // Dump buffered messages from before we opened the log.
        while (!vMsgsBeforeOpenLog.empty()) {
            FileWriteStr(vMsgsBeforeOpenLog.front(), fileout);
            vMsgsBeforeOpenLog.pop_front();
        }
    }
}

struct CLogCategoryDesc {
    TessaLog::LogFlags flag;
    std::string category;
};

const CLogCategoryDesc LogCategories[] = {
    {TessaLog::NONE, "0"},
    {TessaLog::NET, "net"},
    {TessaLog::TOR, "tor"},
    {TessaLog::MEMPOOL, "mempool"},
    {TessaLog::HTTP, "http"},
    {TessaLog::Bench, "bench"},
    {TessaLog::ZMQ, "zmq"},
    {TessaLog::DB, "db"},
    {TessaLog::RPC, "rpc"},
    {TessaLog::ESTIMATEFEE, "estimatefee"},
    {TessaLog::ADDRMAN, "addrman"},
    {TessaLog::SELECTCOINS, "selectcoins"},
    {TessaLog::REINDEX, "reindex"},
    {TessaLog::CMPCTBLOCK, "cmpctblock"},
    {TessaLog::PRUNE, "prune"},
    {TessaLog::PROXY, "proxy"},
    {TessaLog::MEMPOOLREJ, "mempoolrej"},
    {TessaLog::LIBEVENT, "libevent"},
    {TessaLog::COINDB, "coindb"},
    {TessaLog::QT, "qt"},
    {TessaLog::LEVELDB, "leveldb"},
    {TessaLog::ZKP, "zero"},
    {TessaLog::ALL, "1"},
    {TessaLog::ALL, "all"},
};

bool GetLogCategory(TessaLog::LogFlags &flag, const std::string &str) {
    if (str == "") {
        flag = TessaLog::ALL;
        return true;
    }
    for (const CLogCategoryDesc &category_desc : LogCategories) {
        if (category_desc.category == str) {
            flag = category_desc.flag;
            return true;
        }
    }
    return false;
}

std::string ListLogCategories() {
    std::string ret;
    int outcount = 0;
    for (const CLogCategoryDesc &category_desc : LogCategories) {
        // Omit the special cases.
        if (category_desc.flag != TessaLog::NONE &&
            category_desc.flag != TessaLog::ALL) {
            if (outcount != 0) ret += ", ";
            ret += category_desc.category;
            outcount++;
        }
    }
    return ret;
}

TessaLog::Logger::~Logger() {
    if (fileout) {
        fclose(fileout);
    }
}

std::string TessaLog::Logger::LogTimestampStr(const std::string &str) {
    std::string strStamped;

    if (!fLogTimestamps) return str;

    if (fStartedNewLine) {
        int64_t nTimeMicros = GetLogTimeMicros();
        strStamped =
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nTimeMicros / 1000000);
        if (fLogTimeMicros)
            strStamped += strprintf(".%06d", nTimeMicros % 1000000);
        strStamped += ' ' + str;
    } else
        strStamped = str;

    if (!str.empty() && str[str.size() - 1] == '\n')
        fStartedNewLine = true;
    else
        fStartedNewLine = false;

    return strStamped;
}

int TessaLog::Logger::LogPrintStr(const std::string &str) {
    // Returns total number of characters written.
    int ret = 0;

    std::string strTimestamped = LogTimestampStr(str);

    if (fPrintToConsole) {
        // Print to console.
        ret = fwrite(strTimestamped.data(), 1, strTimestamped.size(), stdout);
        fflush(stdout);
    } else if (fPrintToDebugLog) {
        std::lock_guard<std::mutex> scoped_lock(mutexDebugLog);

        // Buffer if we haven't opened the log yet.
        if (fileout == nullptr) {
            ret = strTimestamped.length();
            vMsgsBeforeOpenLog.push_back(strTimestamped);
        } else {
            // Reopen the log file, if requested.
            if (fReopenDebugLog) {
                fReopenDebugLog = false;
                fs::path pathDebug = GetDataDir() / "debug.log";
                if (fsbridge::freopen(pathDebug, "a", fileout) != nullptr) {
                    // unbuffered.
                    setbuf(fileout, nullptr);
                }
            }

            ret = FileWriteStr(strTimestamped, fileout);
        }
    }
    return ret;
}

void TessaLog::Logger::ShrinkDebugFile() {
    // Amount of debug.log to save at end when shrinking (must fit in memory)
    constexpr size_t RECENT_DEBUG_HISTORY_SIZE = 10 * 1000000;
    // Scroll debug.log if it's getting too big.
    fs::path pathLog = GetDataDir() / "debug.log";
    FILE *file = fsbridge::fopen(pathLog, "r");
    // If debug.log file is more than 10% bigger the RECENT_DEBUG_HISTORY_SIZE
    // trim it down by saving only the last RECENT_DEBUG_HISTORY_SIZE bytes.
    if (file &&
        fs::file_size(pathLog) > 11 * (RECENT_DEBUG_HISTORY_SIZE / 10)) {
        // Restart the file with some of the end.
        std::vector<char> vch(RECENT_DEBUG_HISTORY_SIZE, 0);
        fseek(file, -((int64_t)vch.size()), SEEK_END);
        int nBytes = fread(vch.data(), 1, vch.size(), file);
        fclose(file);

        file = fsbridge::fopen(pathLog, "w");
        if (file) {
            fwrite(vch.data(), 1, nBytes, file);
            fclose(file);
        }
    } else if (file != nullptr)
        fclose(file);
}

void TessaLog::Logger::EnableCategory(LogFlags category) {
    logCategories |= category;
}

void TessaLog::Logger::DisableCategory(LogFlags category) {
    logCategories &= ~category;
}

bool TessaLog::Logger::WillLogCategory(LogFlags category) const {
    return (logCategories.load(std::memory_order_relaxed) & category) != 0;
}

bool TessaLog::Logger::DefaultShrinkDebugFile() const {
    return logCategories != TessaLog::NONE;
}
// clang-format on
