/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include <cstdio>

#include "core/Mari.hpp"

int main(int argc, char **argv) {
    std::printf("[Mari      ] PlayStation emulator\n");

    if (argc < 2) {
        std::printf("Usage: Mari /path/to/bios /path/to/iso\n");

        return -1;
    }

    ps::init(argv[1], argv[2]);
    ps::run();

    return 0;
}
