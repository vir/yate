/**
 * main.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 */

#include "telengine.h"

extern "C" int main(int argc, const char **argv, const char **environ)
{
    return TelEngine::Engine::main(argc,argv,environ);
}
