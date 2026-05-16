#pragma once

struct LauncherConfig;

int runApplication(bool debugConsole = false);
int runApplicationFromCLI(const LauncherConfig& cfg, bool debugConsole = false);
