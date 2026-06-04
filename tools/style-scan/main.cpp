// style-scan — batch-parse a whole style library and report which .sty files
// load cleanly, which parse with warnings, and which fail (and why). Walks
// directories recursively, ignores non-.sty files, never crashes on a bad file
// (Unicode names and native parser faults are handled), and writes a CSV report.
//
// Usage: style-scan <dir|file> [<dir|file>...] [--csv <path>]
//   --csv <path>   where to write the report (default: style-scan-report.csv)

#include "Arranger/StyParser.h"
#include "Arranger/Style.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _MSC_VER
#include <eh.h>   // _set_se_translator: turn native faults into C++ exceptions
#endif

namespace fs = std::filesystem;
using namespace cadenza::arranger;

namespace
{
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isStyleFile(const fs::path& p)
{
    // Extensions are ASCII, so this conversion is always safe. Yamaha styles ship
    // under several extensions that all hold the same SFF (SMF+CASM) payload:
    //   .sty (user), .prs (preset), .sst (session), .fps (free play), .bcs.
    const auto ext = lower(p.extension().string());
    return ext == ".sty" || ext == ".prs" || ext == ".sst"
        || ext == ".fps" || ext == ".bcs";
}

// Narrow string for display/CSV that won't throw on non-ANSI (e.g. Cyrillic)
// file names — falls back to the UTF-8 representation.
std::string safePathString(const fs::path& p)
{
    try {
        return p.string();
    } catch (...) {
        const auto u8 = p.u8string();
        return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
    }
}

// A "benign" warning is informational, not a defect: the style still plays as
// intended. Today that's the percussion-routing note (a feature, not a problem).
bool isBenignWarning(const std::string& w)
{
    // Percussion auto-routing and sibling-section policy inheritance are both
    // successful outcomes (real behavior / real Yamaha policy), not defects.
    return w.find("percussion detected, routing") != std::string::npos
        || w.find("inherited NTR/NTT policy") != std::string::npos;
}

std::string normalizeMessage(const std::string& msg)
{
    std::string out;
    bool inDigits = false;
    for (char c : msg) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            if (!inDigits) { out += '#'; inDigits = true; }
        } else {
            out += c;
            inDigits = false;
        }
    }
    return out;
}

std::string csvField(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    out += '"';
    return out;
}

std::string joinWarnings(const std::vector<std::string>& warnings)
{
    std::string out;
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        if (i) out += " | ";
        out += warnings[i];
    }
    return out;
}

// Read a file via an fs::path stream (Unicode-safe on Windows).
bool readAllBytes(const fs::path& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

struct Totals
{
    int total = 0, ok = 0, warn = 0, fail = 0;
    int percussionRouted = 0;   // styles with the benign percussion note
    int usedFallback = 0;       // styles that fell back to heuristic role mapping
    std::map<std::string, int> realWarningBuckets;
    std::map<std::string, int> infoBuckets;
    std::map<std::string, int> errorBuckets;
};

void printTopBuckets(const char* title, const std::map<std::string, int>& buckets, int topN)
{
    if (buckets.empty()) return;
    std::vector<std::pair<std::string, int>> v(buckets.begin(), buckets.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::printf("\n%s\n", title);
    int shown = 0;
    for (const auto& [msg, count] : v) {
        if (shown++ >= topN) break;
        std::printf("  %6d x  %s\n", count, msg.c_str());
    }
}

#ifdef _MSC_VER
void seTranslator(unsigned int, _EXCEPTION_POINTERS*)
{
    throw std::runtime_error("native fault in parser (access violation / bad memory)");
}
#endif
}

int main(int argc, char** argv)
{
#ifdef _MSC_VER
    _set_se_translator(seTranslator);   // requires /EHa; lets catch(...) see faults
#endif

    std::vector<std::string> inputs;
    std::string csvPath = "style-scan-report.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--csv" && i + 1 < argc) { csvPath = argv[++i]; }
        else inputs.push_back(arg);
    }

    if (inputs.empty()) {
        std::printf("usage: style-scan <dir|file> [<dir|file>...] [--csv <path>]\n");
        return 2;
    }

    // Collect .sty files (recursively for directories), tolerating bad entries.
    std::vector<fs::path> files;
    for (const auto& in : inputs) {
        std::error_code ec;
        fs::path root(in);
        if (!fs::exists(root, ec)) {
            std::printf("WARNING: path not found, skipping: %s\n", in.c_str());
            continue;
        }
        if (fs::is_directory(root, ec)) {
            for (auto it = fs::recursive_directory_iterator(
                     root, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) { ec.clear(); continue; }
                try {
                    if (it->is_regular_file(ec) && isStyleFile(it->path()))
                        files.push_back(it->path());
                } catch (...) { /* skip unreadable entry */ }
            }
        } else if (isStyleFile(root)) {
            files.push_back(root);
        }
    }

    std::printf("Found %zu .sty file(s). Scanning...\n", files.size());

    std::ofstream csv(csvPath, std::ios::binary);
    const bool csvOk = csv.good();
    if (csvOk)
        csv << "path,status,sections,parts,warning_count,warnings,error\n";

    Totals t;
    for (const auto& path : files) {
        ++t.total;

        std::string status = "FAIL";
        int sections = 0, parts = 0;
        std::vector<std::string> warnings;
        std::string error;

        try {
            std::vector<uint8_t> bytes;
            if (!readAllBytes(path, bytes)) {
                error = "cannot open file";
            } else if (bytes.empty()) {
                error = "empty file";
            } else {
                auto r = parseStyBytes(bytes);
                if (!r.ok) {
                    error = r.error.empty() ? "parse failed" : r.error;
                } else {
                    sections = static_cast<int>(r.style.sections.size());
                    for (const auto& s : r.style.sections)
                        parts += static_cast<int>(s.parts.size());
                    warnings = r.style.parseWarnings;

                    if (sections == 0 || parts == 0) {
                        status = "FAIL";
                        error = "no playable sections/parts";
                    } else {
                        // Only non-benign warnings make a style "WARN".
                        bool hasReal = false;
                        for (const auto& w : warnings)
                            if (!isBenignWarning(w)) { hasReal = true; break; }
                        status = hasReal ? "WARN" : "OK";
                    }
                }
            }
        } catch (const std::exception& e) {
            error = std::string("exception: ") + e.what();
        } catch (...) {
            error = "exception: unknown";
        }

        if (status == "OK")        ++t.ok;
        else if (status == "WARN") ++t.warn;
        else                       ++t.fail;

        bool sawPercussion = false, sawFallback = false;
        for (const auto& w : warnings) {
            if (isBenignWarning(w)) {
                ++t.infoBuckets[normalizeMessage(w)];
                sawPercussion = true;
            } else {
                ++t.realWarningBuckets[normalizeMessage(w)];
                if (w.find("missing NTR/NTT policy") != std::string::npos)
                    sawFallback = true;
            }
        }
        if (sawPercussion) ++t.percussionRouted;
        if (sawFallback)   ++t.usedFallback;
        if (status == "FAIL") ++t.errorBuckets[normalizeMessage(error)];

        if (csvOk) {
            csv << csvField(safePathString(path)) << ',' << status << ','
                << sections << ',' << parts << ',' << warnings.size() << ','
                << csvField(joinWarnings(warnings)) << ',' << csvField(error) << '\n';
            csv.flush();   // survive a crash on a later file
        }

        if (t.total % 200 == 0)
            std::printf("  ...%d/%zu\n", t.total, files.size());
    }

    std::printf("\n===== Style scan summary =====\n");
    std::printf("  total : %d\n", t.total);
    std::printf("  OK    : %d  (plays cleanly; benign notes only)\n", t.ok);
    std::printf("  WARN  : %d  (plays, but with real unsupported features)\n", t.warn);
    std::printf("  FAIL  : %d  (could not produce a playable style)\n", t.fail);
    std::printf("  playable (OK+WARN): %d of %d (%.1f%%)\n",
                t.ok + t.warn, t.total,
                t.total ? 100.0 * (t.ok + t.warn) / t.total : 0.0);
    std::printf("\n  of those, informational:\n");
    std::printf("    %d had percussion auto-routed to the drum channel (expected)\n", t.percussionRouted);
    std::printf("    %d fell back to heuristic role mapping (no CASM policy)\n", t.usedFallback);

    printTopBuckets("Top REAL warning reasons (count x reason):", t.realWarningBuckets, 15);
    printTopBuckets("Informational notes (count x reason):", t.infoBuckets, 5);
    printTopBuckets("Top failure reasons (count x reason):", t.errorBuckets, 15);

    if (csvOk)
        std::printf("\nFull report written to: %s\n", csvPath.c_str());
    else
        std::printf("\nWARNING: could not write CSV to %s\n", csvPath.c_str());

    return 0;
}
