#include "configure.h"

namespace Memory {

struct BitField {
    uint8_t width;
    uint8_t offset;
    
    /** Retrieve value from address. */
    uint64_t value(uint64_t address) {
        return (address >> offset) & ((1 << width) - 1);
    }
    
    /** Retrieve value from filtering address. */
    uint64_t filter(uint64_t address) {
        return address & (((1 << width) - 1) << offset);
    }
};

class Memory {
public:
    virtual bool addRequest(int64_t clock, uint64_t address, bool is_write) = 0;
};

};