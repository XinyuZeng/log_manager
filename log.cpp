#include "log.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

LogManager::LogManager()
{

}

LogManager::LogManager(const char * log_name)
{
    _buffer_size = 64 * 1024 * 1024;
    int align = 512 - 1;
    _lsn = 0;
    _name_size = 50;
    _log_name = new char[_name_size];
    strcpy(_log_name, log_name);
    //TODO: delete O_TRUNC when recovery is needed.
    _log_fd = open(log_name, O_RDWR | O_CREAT | O_TRUNC | O_APPEND | O_DIRECT, 0755);
    if (_log_fd == 0) {
        perror("open log file");
        exit(1);
    }
    
    // group commit
    latch_ = new std::mutex();
    swap_lock = new std::mutex();
    cv_ = new std::condition_variable();
    appendCv_ = new std::condition_variable();
    buffer_cv_ = new std::condition_variable();
    flush_cv_ = new std::condition_variable();
    latch_return_response_ = new std::mutex();

    _buffer = (char *)malloc(_buffer_size + align); // 64 MB
    _buffer = (char *)(((uintptr_t)_buffer + align)&~((uintptr_t)align));
    flush_buffer_ = (char *)malloc(_buffer_size + align); // 64 MB
    flush_buffer_ = (char *)(((uintptr_t)flush_buffer_ + align)&~((uintptr_t)align));
}

LogManager::~LogManager() {
		delete[] _log_name;
		_log_name = nullptr;
		close(_log_fd);
}

void
LogManager::test() {
    printf("test\n");
}


RC LogManager::log(const HelloRequest* request, HelloReply* reply) {
    // TODO: add logic for insert once
    log_request(request, reply);
    return RCOK;
}

// if no log return INVALID else return the log type
/*
LogRecord::Type LogManager::check_log(Message * msg) {
    LogRecord::Type vote = LogRecord::INVALID;
    uint16_t watermark = msg->get_lsn();
    if (watermark == -1) {
        // no log for this txn
        return vote;
    }
    FILE * fp = fopen(_log_name, "r");
    fseek(fp, 0, SEEK_END);
    fseek(fp, -sizeof(LogRecord), SEEK_CUR);
    LogRecord cur_log;
    if (fread((void *)&cur_log, sizeof(LogRecord), 1, fp) != 1) {
        return vote;
    }

    while (cur_log.get_latest_lsn() > watermark) {
        //TODO: whether to add log type equals?
        if (cur_log.get_txn_id() == msg->get_txn_id() && 
        log_to_message(cur_log.get_log_record_type()) == msg->get_type()) {
            // log exists
            vote = cur_log.get_log_record_type();
            break;
        }

        if (fseek(fp, -2 * sizeof(LogRecord), SEEK_CUR) == -1)
            break;
        assert(fread((void *)&cur_log, sizeof(LogRecord), 1, fp) == 1);
    }
    fseek(fp, 0, SEEK_END);
    fclose(fp);
    return vote;
}
*/

void LogManager::log_request(const HelloRequest* request, HelloReply * reply) {
    std::unique_lock<std::mutex> latch(*latch_);
    ATOM_FETCH_ADD(_lsn, 1);
    uint32_t size_total = sizeof(LogRecord) + request->data_size();
    swap_lock->lock();
    if (logBufferOffset_ + size_total > _buffer_size) {
        needFlush_ = true;
        cv_->notify_one(); //let RunFlushThread wake up.
        appendCv_->wait(latch, [&] {return logBufferOffset_ <= _buffer_size;});
        // logBufferOffset_ = 0;
    }
    LogRecord log{request->node_id(), request->txn_id(), 
                _lsn, request_to_log(request->request_type())};
    // format: | node_id | txn_id | lsn | type | size of data(if any) | data(if any)
    memcpy(_buffer + logBufferOffset_, &log, sizeof(log));
    logBufferOffset_ += sizeof(LogRecord);
    if (request->data_size() != 0) {
        uint64_t data_size = request->data_size();
        memcpy(_buffer + logBufferOffset_, &data_size, sizeof(uint64_t));
        logBufferOffset_ += sizeof(uint64_t);
        memcpy(_buffer + logBufferOffset_, request->data().c_str(), request->data_size());
        logBufferOffset_ += request->data_size();
    }
    swap_lock->unlock();
    // TODO: sleep until flush finish and wakeup
    std::unique_lock<std::mutex> latch2(*latch_return_response_);
    buffer_cv_->wait(latch2);
    reply->set_reply_type(HelloReply::ACK);
}

uint64_t LogManager::get_last_lsn() {
    return _lsn;
}

HelloRequest::RequestType LogManager::log_to_request(LogRecord::Type vote) {
    switch (vote)
    {
    case LogRecord::INVALID :
        return HelloRequest:: LOG_ACK;
    case LogRecord::COMMIT :
        return HelloRequest:: LOG_COMMIT;
    case LogRecord::ABORT :
        return HelloRequest:: LOG_ABORT;
    case LogRecord::YES :
        return HelloRequest:: LOG_YES;
    default:
        assert(false);
    }
}

LogRecord::Type LogManager::request_to_log(HelloRequest::RequestType vote) {
    switch (vote)
    {
    case HelloRequest:: COMMIT:
        return LogRecord::COMMIT;
    case HelloRequest:: ABORT:
        return LogRecord::ABORT;
    case HelloRequest:: YES:
        return LogRecord::YES;
    default:
        assert(false);
    }
}
//group commit
// spawn a separate thread to wake up periodically to flush
void LogManager::run_flush_thread() {
    if (ENABLE_LOGGING) return;
    ENABLE_LOGGING = true;
    flush_thread_ = new std::thread([&] {
        while (ENABLE_LOGGING) { //The thread is triggered every LOG_TIMEOUT seconds or when the log buffer is full
        std::unique_lock<std::mutex> latch(*latch_);
            // (2) When LOG_TIMEOUT is triggered.
            cv_->wait_for(latch, log_timeout, [&] {return needFlush_;});
            assert(flushBufferSize_ == 0);
            if (logBufferOffset_ > 0) {

                swap_lock->lock();
                std::swap(_buffer,flush_buffer_);
                std::swap(logBufferOffset_,flushBufferSize_);
                std::swap(buffer_cv_,flush_cv_);
                swap_lock->unlock();
                
                uint64_t aligned_size = PGROUNDUP(flushBufferSize_);
                if (write(_log_fd, flush_buffer_, PGROUNDUP(flushBufferSize_)) == -1) {
                    perror("write2");
                    exit(1);
                }
                // }
                if (fsync(_log_fd) == -1) {
                    perror("fsync");
                    exit(1);
                }

                flushBufferSize_ = 0;
                // TODO: finish flushing and notify all
                flush_cv_->notify_all();
            }
            needFlush_ = false;
            appendCv_->notify_all();
        }
    });
};
/*
 * Stop and join the flush thread, set ENABLE_LOGGING = false
 */
void LogManager::stop_flush_thread() {
    if (!ENABLE_LOGGING) return;
    ENABLE_LOGGING = false;
    flush(true);
    flush_thread_->join();
    assert(logBufferOffset_ == 0 && flushBufferSize_ == 0);
    delete flush_thread_;
};

void LogManager::flush(bool force) {
    std::unique_lock<std::mutex> latch(*latch_);
  if (force) {
    needFlush_ = true;
    cv_->notify_one(); //let RunFlushThread wake up.
    if (ENABLE_LOGGING)
      appendCv_->wait(latch, [&] { return !needFlush_; }); //block append thread
  } else {
    appendCv_->wait(latch);// group commit,  But instead of forcing flush,
    // you need to wait for LOG_TIMEOUT or other operations to implicitly trigger the flush operations
  }
}

