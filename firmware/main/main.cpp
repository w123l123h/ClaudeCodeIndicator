#include "application.h"

extern "C" void app_main(void)
{
    Application::instance().init();
    Application::instance().run();
}