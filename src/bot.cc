#include <chen/application.h>
#include <chen/log/log.h>

static chen::Logger::ptr logger = LOG_NAME("bot");

int main(int argc, char** argv) {
    int ret = 0;
    try {
        srand(time(0));
        chen::Application app;
        if (app.init(argc, argv)) {
            ret = app.run();
        }
    } catch (const std::exception& e) {
        ERROR(logger) << "Exception: " << e.what();
    }

    std::_Exit(ret);
}