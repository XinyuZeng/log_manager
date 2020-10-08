#include "log.h"
#include "global.h"
#include <stdio.h>

LogManager::LogManager() {

}

void
LogManager::test() {
    printf("test\n");
}

int main() {
    log_manager = new LogManager();
    log_manager->test();
    return 0;
}
