// sty-to-cstyle — convert Yamaha .sty files (or plain .mid files) into
// Cadenza's .cstyle JSON format.
//
// Usage:
//   sty-to-cstyle input.sty output.cstyle
//   sty-to-cstyle input.sty output.cstyle --name "Pop 8 Beat" --id pop-8-beat
//   sty-to-cstyle input.sty output.cstyle --verbose

#include "Arranger/StyParser.h"
#include "Arranger/StyleLoader.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
void printUsage()
{
    std::printf(
        "Usage:\n"
        "  sty-to-cstyle <input.sty> <output.cstyle> [options]\n"
        "\n"
        "Options:\n"
        "  --name <text>      Override the style's display name\n"
        "  --id <slug>        Override the style id\n"
        "  --tempo <bpm>      Default tempo if file has no tempo meta event (default: 120)\n"
        "  --verbose          Print parse diagnostics to stderr\n"
        "  -h, --help         Show this help\n"
    );
}
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        printUsage();
        return (argc == 2 && (!std::strcmp(argv[1], "-h") || !std::strcmp(argv[1], "--help"))) ? 0 : 1;
    }

    const std::string inputPath  = argv[1];
    const std::string outputPath = argv[2];

    cadenza::arranger::StyParseOptions options;

    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--name"  && i + 1 < argc) options.overrideName = argv[++i];
        else if (a == "--id"    && i + 1 < argc) options.overrideId   = argv[++i];
        else if (a == "--tempo" && i + 1 < argc) options.defaultTempo = std::atoi(argv[++i]);
        else if (a == "--verbose") options.verbose = true;
        else if (a == "-h" || a == "--help") { printUsage(); return 0; }
        else { std::fprintf(stderr, "Unknown option: %s\n", a.c_str()); printUsage(); return 1; }
    }

    auto result = cadenza::arranger::parseStyFile(inputPath, options);
    if (!result.ok) {
        std::fprintf(stderr, "Parse failed: %s\n", result.error.c_str());
        return 2;
    }

    const auto& s = result.style;
    if (!cadenza::arranger::saveStyleToFile(s, outputPath, /*pretty=*/true)) {
        std::fprintf(stderr, "Could not write: %s\n", outputPath.c_str());
        return 3;
    }

    int totalParts = 0, totalNotes = 0;
    for (const auto& sec : s.sections) {
        totalParts += static_cast<int>(sec.parts.size());
        for (const auto& p : sec.parts) totalNotes += static_cast<int>(p.notes.size());
    }
    std::printf("OK: %s (%d sections, %d parts, %d notes) -> %s\n",
                s.name.c_str(),
                static_cast<int>(s.sections.size()), totalParts, totalNotes,
                outputPath.c_str());
    return 0;
}
