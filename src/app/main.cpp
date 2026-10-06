#include "app/Application.hpp"

#include "core/Log.hpp"

int main() {
    s3d::Application app;
    if (!app.initialize()) return 1;
    return app.run();
}