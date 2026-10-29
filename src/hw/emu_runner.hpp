#ifndef CHIAVDF_EMU_RUNNER_HPP
#define CHIAVDF_EMU_RUNNER_HPP

#include <cstdint>

int emu_do_io(uint8_t *buf_in, uint16_t size_in, uint8_t *buf_out, uint16_t size_out);
int emu_do_io_i2c(uint8_t *buf, uint16_t size, uint32_t addr, int is_out);

// Scoped, thread-local emulator transport trace for initialization tests. It is
// disabled outside the fixture and is never linked into the real USB client.
#include <vector>
struct EmuOperation {
    enum Kind { reset, read, write, i2c_write } kind;
    uint32_t address = 0;
    uint32_t value = 0;
};
class EmuTrace {
public:
    EmuTrace();
    ~EmuTrace();
    EmuTrace(const EmuTrace &) = delete;
    EmuTrace &operator=(const EmuTrace &) = delete;
    std::vector<EmuOperation> operations;
private:
    EmuTrace *previous_;
};
void emu_reset_board();
#endif
