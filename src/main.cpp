#include "ConsoleApp.h"
#include "GUIapp.h"

int main(int argc, char *argv[])
{
    if (argc == 1)
        return GUIapp::start();
    else
        return ConsoleApp::start(argc, argv);
}
