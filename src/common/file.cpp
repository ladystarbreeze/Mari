/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "file.hpp"

#include <fstream>
#include <iterator>

std::vector<u8> loadBinary(const char *path) {
    std::ifstream file{path, std::ios::binary};

    file.unsetf(std::ios::skipws);

    return {std::istream_iterator<u8>{file}, {}};
}
