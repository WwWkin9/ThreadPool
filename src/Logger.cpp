#include "logger/logger.h"
#include <iostream>

void Logger::info(const std::string& msg) {
    std::cout << "[INFO] " << msg << std::endl;
}

void Logger::error(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
}