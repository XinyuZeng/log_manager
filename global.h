#pragma once
#include <vector>
class LogManager;
extern LogManager * log_manager;
enum RC {RCOK, COMMIT, ABORT, WAIT, LOCAL_MISS, SPECULATE, ERROR, FINISH};
