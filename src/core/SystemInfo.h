#pragma once

// Best-effort, dependency-light system capability probes used to tune
// memory- and CPU-hungry paths (decoder threading, cache budgets, export
// thread counts) for weaker / older machines. All probes degrade gracefully:
// if a value can't be determined the caller simply keeps its current default.
//
// Kept header-only (inline) so it can be included by Decoder.cpp and
// Exporter.cpp without adding a new translation unit to every test target's
// explicit source list in CMakeLists.txt.

#include <cstdint>
#include <QtGlobal>
#include <QString>
#include <QFile>
#include <QByteArray>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace hc {
namespace systeminfo {

// Returns the value of a single /proc/meminfo field in KiB, or 0 if the
// field isn't present or the file can't be read.
inline uint64_t memInfoKb(const char* field) {
#if defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    const QByteArray needle(field);
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        if (!line.startsWith(needle)) continue;
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QByteArray rest = line.mid(colon + 1).trimmed();
        const int sp = rest.indexOf(' ');
        const QByteArray numPart = (sp > 0) ? rest.left(sp) : rest;
        bool ok = false;
        const qulonglong v = numPart.toULongLong(&ok);
        return (ok && v > 0) ? static_cast<uint64_t>(v) : 0;
    }
    return 0;
#else
    (void)field;
    return 0;
#endif
}

// Total physical RAM in bytes, or 0 if unknown.
inline uint64_t totalMemoryBytes() {
    const uint64_t kb = memInfoKb("MemTotal:");
    if (kb > 0) return kb * 1024ULL;
#if defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    const long pages = ::sysconf(_SC_PHYS_PAGES);
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) {
        return static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize);
    }
#endif
    return 0;
}

// Currently *available* RAM in bytes (Linux only; 0 elsewhere — callers must
// treat 0 as "unknown", not "empty").
inline uint64_t availableMemoryBytes() {
    const uint64_t kb = memInfoKb("MemAvailable:");
    return kb > 0 ? kb * 1024ULL : 0;
}

// Number of online CPU cores (>= 1). Falls back to 1 if the OS can't tell.
inline int cpuCoreCount() {
#if defined(_SC_NPROCESSORS_ONLN)
    const long n = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) return static_cast<int>(n);
#endif
    return 1;
}

} // namespace systeminfo
} // namespace hc
