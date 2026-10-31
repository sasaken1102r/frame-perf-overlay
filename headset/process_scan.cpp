#include "process_scan.h"

#include "hide_rules.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

bool parseProcStat(const char* text, size_t length, ProcStat& out) {
    const char* end = text + length;
    const char* open = static_cast<const char*>(std::memchr(text, '(', length));
    if (open == nullptr) return false;
    // comm may contain ')' itself: the real end is the last ')'
    const char* close = nullptr;
    for (const char* p = end; p > open;) {
        --p;
        if (*p == ')') {
            close = p;
            break;
        }
    }
    if (close == nullptr) return false;
    out.comm.assign(open + 1, close);
    // Fields after ") ": index 0 = field 3 (state). We need 14, 15, 22, 24 → indexes 11, 12, 19, 21
    const char* p = close + 1;
    uint64_t values[22] = {};
    int index = 0;
    while (index < 22) {
        while (p < end && *p == ' ') ++p;
        if (p >= end) return false;
        const char* tokenStart = p;
        while (p < end && *p != ' ' && *p != '\n') ++p;
        if (index > 0) {  // index 0 is the state letter
            char buffer[32];
            const size_t n = std::min<size_t>(static_cast<size_t>(p - tokenStart), sizeof(buffer) - 1);
            std::memcpy(buffer, tokenStart, n);
            buffer[n] = '\0';
            // Signed fields (priority, nice, ...) aren't ones we keep; rss can't be negative in practice
            values[index] = static_cast<uint64_t>(std::strtoll(buffer, nullptr, 10));
        }
        ++index;
    }
    out.utime = values[11];
    out.stime = values[12];
    out.starttime = values[19];
    out.rssPages = static_cast<int64_t>(values[21]);
    return true;
}

std::string processDisplayName(const std::string& comm, const std::string& exe) {
    constexpr size_t kCommMax = 15;  // TASK_COMM_LEN - 1
    const bool commCut = comm.size() == kCommMax && exe.size() > kCommMax && exe.compare(0, kCommMax, comm) == 0;
    const bool exeShorter = !exe.empty() && exe.size() < comm.size() && comm.compare(0, exe.size(), exe) == 0;
    return commCut || exeShorter ? exe : comm;
}

std::vector<ProcessRow> topRows(std::vector<ProcessRow> rows, bool sumSameName, size_t max, bool byCpu) {
    if (sumSameName) {
        std::map<std::string, ProcessRow> merged;
        for (const ProcessRow& r : rows) {
            auto it = merged.find(r.name);
            if (it == merged.end()) {
                ProcessRow m = r;
                m.pid = 0;
                merged.emplace(r.name, m);
            } else {
                it->second.count += r.count;
                it->second.cpuCorePct += r.cpuCorePct;
                it->second.rssMiB += r.rssMiB;
            }
        }
        rows.clear();
        for (auto& entry : merged) rows.push_back(std::move(entry.second));
    }
    const auto better = [byCpu](const ProcessRow& a, const ProcessRow& b) {
        const double ka = byCpu ? a.cpuCorePct : a.rssMiB;
        const double kb = byCpu ? b.cpuCorePct : b.rssMiB;
        if (ka != kb) return ka > kb;
        if (a.name != b.name) return a.name < b.name;
        return a.pid < b.pid;
    };
    if (rows.size() > max) {
        std::partial_sort(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(max), rows.end(), better);
        rows.resize(max);
    } else {
        std::sort(rows.begin(), rows.end(), better);
    }
    return rows;
}

ProcessScanner::ProcessScanner(std::string procRoot, long ticksPerSecond, long pageSize)
    : root_(std::move(procRoot)),
      ticksPerSecond_(ticksPerSecond > 0 ? ticksPerSecond : ::sysconf(_SC_CLK_TCK)),
      pageSize_(pageSize > 0 ? pageSize : ::sysconf(_SC_PAGESIZE)) {
    if (ticksPerSecond_ <= 0) ticksPerSecond_ = 100;
    if (pageSize_ <= 0) pageSize_ = 4096;
}

void ProcessScanner::reset() {
    previous_.clear();
    previousTime_ = -1.0;
}

const ProcessLists& ProcessScanner::scan(double now, const ProcessListOptions& options) {
    const auto wallStart = std::chrono::steady_clock::now();
    ProcessLists lists;
    std::vector<ProcessRow> rows;
    std::unordered_map<int, Previous> current;
    current.reserve(previous_.size() + 64);
    const double elapsed = previousTime_ >= 0 ? now - previousTime_ : 0.0;
    const bool haveInterval = elapsed > 0.0;

    DIR* dir = ::opendir(root_.c_str());
    if (dir != nullptr) {
        char path[512];
        char buffer[1024];
        while (dirent* entry = ::readdir(dir)) {
            const char* name = entry->d_name;
            if (name[0] < '1' || name[0] > '9') continue;  // only pid directories
            char* tail = nullptr;
            const long pid = std::strtol(name, &tail, 10);
            if (*tail != '\0' || pid <= 0) continue;
            std::snprintf(path, sizeof(path), "%s/%s/stat", root_.c_str(), name);
            // The process may have exited since readdir: any failure from here on is that race, and is skipped
            const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                ++lists.vanished;
                continue;
            }
            const ssize_t n = ::read(fd, buffer, sizeof(buffer));
            ::close(fd);
            ProcStat stat;
            if (n <= 0 || !parseProcStat(buffer, static_cast<size_t>(n), stat)) {
                ++lists.vanished;
                continue;
            }
            ++lists.scanned;
            const uint64_t ticks = stat.utime + stat.stime;
            current[static_cast<int>(pid)] = {stat.starttime, ticks};

            ProcessRow row;
            row.pid = static_cast<int>(pid);
            row.exe = exeName(row.pid, stat.starttime, stat.comm);
            row.name = processDisplayName(stat.comm, row.exe);
            row.comm = std::move(stat.comm);
            row.rssMiB = static_cast<double>(stat.rssPages) * static_cast<double>(pageSize_) / (1024.0 * 1024.0);
            row.cpuCorePct = 0.0;
            if (haveInterval) {
                const auto it = previous_.find(row.pid);
                // A new process, or a reused pid, has no baseline yet: 0% this time, a real value next scan
                if (it != previous_.end() && it->second.starttime == stat.starttime && ticks >= it->second.ticks) {
                    row.cpuCorePct = static_cast<double>(ticks - it->second.ticks) / static_cast<double>(ticksPerSecond_) /
                                 elapsed * 100.0;
                }
            }
            rows.push_back(std::move(row));
        }
        ::closedir(dir);
    }

    lists.cpuValid = haveInterval && dir != nullptr;
    // Hide system drops what the rules name before ranking, so the list still fills its rows
    const bool hiding = hideRules_ != nullptr && (options.cpuHideSystem || options.ramHideSystem);
    std::vector<ProcessRow> visible;
    if (hiding) {
        hideRules_->refresh(now);
        visible = rows;
        dropHidden(visible, current);
        lists.hideRulesError = hideRules_->loadFailed();
    }
    if (lists.cpuValid) {
        lists.byCpu = topRows(hiding && options.cpuHideSystem ? visible : rows, options.sumSameName, options.cpuMax, true);
    }
    const bool ramSum = options.ramSumSameName < 0 ? options.sumSameName : options.ramSumSameName != 0;
    lists.byRam = topRows(hiding && options.ramHideSystem ? std::move(visible) : std::move(rows), ramSum, options.ramMax,
                          false);
    for (auto it = exeNames_.begin(); it != exeNames_.end();) {
        it = current.count(it->first) != 0 ? std::next(it) : exeNames_.erase(it);
    }
    for (auto it = hidden_.begin(); it != hidden_.end();) {
        it = current.count(it->first) != 0 ? std::next(it) : hidden_.erase(it);
    }
    previous_ = std::move(current);
    previousTime_ = dir != nullptr ? now : -1.0;
    lists.scanMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wallStart).count();
    lists_ = std::move(lists);
    return lists_;
}

void ProcessScanner::dropHidden(std::vector<ProcessRow>& rows, const std::unordered_map<int, Previous>& current) {
    char path[512];
    char buffer[4096];
    const uint64_t generation = hideRules_->generation();
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const ProcessRow& row) {
        const auto cur = current.find(row.pid);
        const uint64_t start = cur != current.end() ? cur->second.starttime : 0;
        auto it = hidden_.find(row.pid);
        if (it == hidden_.end() || it->second.starttime != start || it->second.comm != row.comm) {
            // New process (or it exec'd): its cgroup, once. Unreadable (it exited) = ""
            Hidden h;
            h.starttime = start;
            h.comm = row.comm;
            std::snprintf(path, sizeof(path), "%s/%d/cgroup", root_.c_str(), row.pid);
            const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
            if (fd >= 0) {
                const ssize_t n = ::read(fd, buffer, sizeof(buffer));
                ::close(fd);
                if (n > 0) h.cgroup = cgroupV2Path(std::string(buffer, static_cast<size_t>(n)));
            }
            it = hidden_.insert_or_assign(row.pid, std::move(h)).first;
        }
        Hidden& h = it->second;
        if (h.generation != generation) {
            h.hidden = hideRules_->hides(row.comm, row.exe, h.cgroup);
            h.generation = generation;
        }
        return h.hidden;
    }), rows.end());
}

const std::string& ProcessScanner::exeName(int pid, uint64_t starttime, const std::string& comm) {
    auto it = exeNames_.find(pid);
    if (it == exeNames_.end() || it->second.starttime != starttime || it->second.comm != comm) {
        // Only our own processes' exe links are readable (others: EACCES), and kernel threads have none
        char path[512];
        char target[4096];
        std::snprintf(path, sizeof(path), "%s/%d/exe", root_.c_str(), pid);
        const ssize_t n = ::readlink(path, target, sizeof(target) - 1);
        std::string name;
        if (n > 0) {
            std::string full(target, static_cast<size_t>(n));
            const std::string deleted = " (deleted)";
            if (full.size() > deleted.size() && full.compare(full.size() - deleted.size(), deleted.size(), deleted) == 0) {
                full.resize(full.size() - deleted.size());
            }
            name = full.substr(full.find_last_of('/') + 1);
        }
        it = exeNames_.insert_or_assign(pid, ExeName {starttime, comm, name}).first;
    }
    return it->second.name;
}
