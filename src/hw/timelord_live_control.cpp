#include "timelord_live_control.hpp"
#ifdef _WIN32
namespace timelord_control {
bool Channel::supported() { return false; }
bool Channel::start() { return false; }
bool Channel::poll(const std::function<Point(Point)> &) { return false; }
std::ptrdiff_t write_no_sigpipe(int, const void *, size_t) { return -1; }
} // namespace timelord_control
#else
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
namespace timelord_control {
std::ptrdiff_t write_no_sigpipe(int fd, const void *data, size_t size) {
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &blocked, &previous)) {
        errno = EIO;
        return -1;
    }
    sigpending(&pending);
    const bool already_pending = sigismember(&pending, SIGPIPE);
    const auto result = write(fd, data, size);
    const int error = errno;
    if (result < 0 && error == EPIPE && !already_pending) {
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE)) {
            int signal;
            sigwait(&blocked, &signal);
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    errno = error;
    return result;
}
bool Channel::supported() { return true; }
bool Channel::start() {
    struct stat input{}, output{};
    if (fstat(input_, &input) || fstat(output_, &output) || !S_ISFIFO(input.st_mode) ||
        !S_ISFIFO(output.st_mode))
        return false;
    const int in_flags = fcntl(input_, F_GETFL), out_flags = fcntl(output_, F_GETFL);
    return in_flags >= 0 && out_flags >= 0 && !fcntl(input_, F_SETFL, in_flags | O_NONBLOCK) &&
           !fcntl(output_, F_SETFL, out_flags | O_NONBLOCK);
}
bool Channel::reply(uint64_t id, const char *error, Point actual) {
    char response[512];
    const int size = error ? snprintf(response, sizeof(response), "TLERR 1 %llu %.400s\n",
                                      (unsigned long long)id, error)
                           : snprintf(response, sizeof(response), "TLACK 1 %llu %.6f %.6f\n",
                                      (unsigned long long)id, actual.frequency, actual.voltage);
    return size > 0 && size_t(size) < sizeof(response) &&
           write_no_sigpipe(output_, response, size_t(size)) == size;
}
bool Channel::poll(const std::function<Point(Point)> &apply) {
    char data[Limits::frame_bytes];
    const auto count = read(input_, data, sizeof(data));
    if (!count || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        return false;
    if (count > 0)
        pending_.append(data, size_t(count));
    if (pending_.size() > Limits::frame_bytes)
        return false;
    const auto end = pending_.find('\n');
    if (end == std::string::npos)
        return true;
    auto request = parse_tlset(std::string_view(pending_).substr(0, end));
    pending_.erase(0, end + 1);
    if (!request || request->id <= last_id_)
        return false;
    last_id_ = request->id;
    try {
        return reply(request->id, nullptr, apply(request->target));
    } catch (const std::exception &error) {
        (void)reply(request->id, error.what());
    } catch (...) {
        (void)reply(request->id, "unknown");
    }
    return false;
}
} // namespace timelord_control
#endif
