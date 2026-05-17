#include "cli.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
    try {
        return hisol::run_cli(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << "hisol: " << ex.what() << '\n';
        std::cerr << "Try 'hisol --help'.\n";
        return EXIT_FAILURE;
    }
}
