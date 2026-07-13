#include "Application.hpp"
#include "LauncherWindow.hpp"
#include <cstring>

int main(int argc, char* argv[]) {
    bool debug = false;
    LauncherConfig cli;

    // Parse CLI arguments
    for (int i = 1; i < argc; i++) {
             if (strcmp(argv[i], "--debug") == 0)          debug = true;
        else if (strcmp(argv[i], "--level") == 0     && i+1<argc) cli.levelPath = argv[++i];
        else if (strcmp(argv[i], "--music") == 0     && i+1<argc) cli.musicPath = argv[++i];
        else if (strcmp(argv[i], "--width") == 0     && i+1<argc) cli.resolutionW = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0    && i+1<argc) cli.resolutionH = atoi(argv[++i]);
        else if (strcmp(argv[i], "--fullscreen") == 0)           cli.fullscreen = true;
        else if (strcmp(argv[i], "--fill") == 0       && i+1<argc) cli.trackFillColor = argv[++i];
        else if (strcmp(argv[i], "--stroke") == 0     && i+1<argc) cli.trackStrokeColor = argv[++i];
        else if (strcmp(argv[i], "--bg") == 0         && i+1<argc) cli.backgroundColor = argv[++i];
        else if (strcmp(argv[i], "--no-auto-stroke") == 0)        cli.autoStroke = false;
        else if (strcmp(argv[i], "--no-hitsound") == 0)           cli.enableHitsounds = false;
        else if (strcmp(argv[i], "--force-hitsound") == 0)      cli.forceHitsounds = true;
        else if (strcmp(argv[i], "--auto-play") == 0)           cli.autoPlay = true;
        else if (strcmp(argv[i], "--legacy-culling") == 0)   cli.legacyCulling = true;
        else if (strcmp(argv[i], "--msaa") == 0)                  cli.msaa = true;
        else if (strcmp(argv[i], "--no-trail") == 0)              cli.showTrail = false;
        else if (strcmp(argv[i], "--export") == 0)                cli.exportHitsounds = true;
    }

    if (cli.exportHitsounds && !cli.levelPath.empty()) {
        return runApplicationFromCLI(cli, debug);
    }
    if (!cli.levelPath.empty()) {
        return runApplicationFromCLI(cli, debug);
    }
    return runApplication(debug);
}
