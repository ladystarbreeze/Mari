/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include <cstdio>

#include "core/Mari.hpp"

int main(int argc, char **argv) {
    std::printf("[Mari      ] PlayStation emulator\n");

    if (argc < 3) {
        std::printf("Usage: Mari /path/to/bios /path/to/iso\n");

        return -1;
    }

    ps::init(argv[1], argv[2], (argc == 4) ? argv[3] : NULL);
    ps::run();

    return 0;
}
