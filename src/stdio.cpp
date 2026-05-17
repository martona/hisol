#include "stdio.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include <cstdio>

namespace hisol {

void set_stdout_binary()
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

void set_stdin_binary()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
}

} // namespace hisol
