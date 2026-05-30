#include <chen/application.h>
#include <chen/log/log.h>
#include <random>

static chen::Logger::ptr logger = LOG_NAME("bot");

int main(int argc, char** argv) {
    try {
        std::random_device rd;
        srand(rd());

        if (chen::Application app; app.init(argc, argv)) {
            return app.run();
        }
    } catch (const std::exception& e) {
        ERROR(logger) << "Exception: " << e.what();
    }
    return 0;
}