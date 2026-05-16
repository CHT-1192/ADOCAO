#include "Application.h"
#include <cstring>

int main(int argc, char* argv[]) {
    bool debug = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) debug = true;
    }
    return runApplication(debug);
}
