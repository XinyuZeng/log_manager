#include <stdio.h>
#include <string.h>
#include "log_record.h"

int main() {
    FILE *fp = fopen("test_log", "r");
    LogRecord log;
    fread(&log, sizeof(log), 1, fp);
    printf("log type:%d\n", log.get_log_record_type());
    uint64_t size;
    fread(&size, sizeof(size), 1, fp);
    char data[size];
    fread(data, size, 1, fp);
    printf("data: %s\n", data);

    fclose(fp);
    return 0;
}
