#include "container.h"
#include <stdint.h>
#include <ostream>
#include <string>
#include <map>

namespace DRAM {

struct BitField {
    uint8_t width;
    uint8_t offset;
    
    /** Retrieve value from address. */
    uint64_t value(uint64_t address) {
        return (address >> offset) & ((1 << width) - 1);
    }
};

struct AddressMapping {
    BitField channel;
    BitField rank;
    BitField bank;
    BitField row;
    BitField column;
};

struct ChannelTiming {
    uint32_t read_to_read;
    uint32_t read_to_write;
    uint32_t write_to_read;
    uint32_t write_to_write;
};

struct RankTiming {
    uint32_t act_to_act;
    uint32_t act_to_faw;
    uint32_t read_to_read;
    uint32_t read_to_write;
    uint32_t write_to_read;
    uint32_t write_to_write;
    
    uint32_t refresh_to_act;
};

struct BankTiming {
    uint32_t act_to_read;
    uint32_t act_to_write;
    uint32_t act_to_pre;
    uint32_t read_to_pre;
    uint32_t write_to_pre;
    uint32_t pre_to_act;
    
    uint32_t read_to_data;
    uint32_t write_to_data;
};

struct Timing {
    uint32_t transaction_delay;
    uint32_t command_delay;
    
    ChannelTiming channel;
    RankTiming rank;
    BankTiming bank;
};

struct Energy {
    float act;
    float pre;
    float read;
    float write;
    float refresh;
    
    float powerup_per_cycle;
    float powerdown_per_cycle;
};

struct Policy {
    uint8_t max_row_idle;
    uint8_t max_row_hits;
};

struct Config {    
    AddressMapping mapping;
    Timing timing;
    Energy energy;
    Policy policy;
    
    uint32_t nChannel;
    uint32_t nRank;
    uint32_t nBank;
    
    uint32_t nTransaction;
    uint32_t nCommand;
    
    Config(std::map<std::string, int> config);
};

struct Coordinates {
    uint8_t channel;
    uint8_t rank;
    uint8_t bank;
    uint32_t row;
    uint32_t column;
    
    friend std::ostream &operator <<(std::ostream &os, Coordinates &coordinates) {
        os << "{"
           << "channel: " << (int)coordinates.channel 
           << ", rank: " << (int)coordinates.rank 
           << ", bank: " << (int)coordinates.bank 
           << ", row: " << (int)coordinates.row 
           << ", column: " << (int)coordinates.column
           << "}";
        return os;
    }
};

/** DRAM transaction */
struct Transaction : public Coordinates {
    uint64_t address;
    bool is_write;
    bool is_pending;
    bool is_finished;
    /** The time when the transaction is ready for translating commands. */
    int64_t readyTime;
    /** The time when the all commands of the transaction is done. */
    int64_t finishTime;
    
    friend std::ostream &operator <<(std::ostream &os, Transaction &transaction) {
        os << "{"
           << "is_write: " << transaction.is_write 
           << ", address: 0x" << std::hex << transaction.address  << std::dec
           << ", coordinates: " << (Coordinates &)transaction 
           << "}";
        return os;
    }
};

/** DRAM command types */
enum CommandType {
    COMMAND_act, /**< row activation */
    COMMAND_pre, /**< row precharge */
    COMMAND_read, /**< column read */
    COMMAND_write, /**< column write */
    COMMAND_read_pre, /**< column read with auto row precharge */
    COMMAND_write_pre, /**< column write with auto row precharge */
    COMMAND_refresh, /**< rank refresh */
    COMMAND_powerdown, /**< rank powerdown */
    COMMAND_powerup, /**< rank powerup */
};

/** DRAM command */
struct Command {
    /** DRAM command type */
    CommandType type;
    /** The time when the command is sent through a channel. */
    int64_t readyTime;
    /** The time when the data is received from or send through a channel. */
    int64_t finishTime;
    /** The cooresponding transaction of the command. */
    Transaction *transaction;
    
    friend std::ostream &operator <<(std::ostream &os, Command &command) {
        os << "{"
           << "type: " << command.type 
           << ", readyTime: " << command.readyTime 
           << ", finishTime: " << command.finishTime 
           << ", transaction: " << *(command.transaction)
           << "}";
        return os;
    }
};

struct RowBuffer {
    int32_t tag;
    uint8_t hits;
};

class Bank
{
protected:
    Config *config;
    
    RowBuffer rowBuffer;
    
    int64_t actReadyTime;
    int64_t preReadyTime;
    int64_t readReadyTime;
    int64_t writeReadyTime;
    
    uint32_t actCount;
    uint32_t preCount;
    uint32_t readCount;
    uint32_t writeCount;
    
public:
    Bank(Config *_config);    
    virtual ~Bank();
    
    inline const RowBuffer &getRowBuffer(Coordinates &coordinates);
    inline const int64_t getReadyTime(CommandType type, Coordinates &coordinates);
    inline int64_t getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates);
    
    friend std::ostream &operator <<(std::ostream &os, Bank &bank) {
        os << "{" << "access: " << (bank.readCount+bank.writeCount)
           << ", act: " << bank.actCount << ", pre: " << bank.preCount 
           << ", read: " << bank.readCount << ", write: " << bank.writeCount << "}";
        return os;
    }
};

class Rank
{
protected:
    Config *config;
    
    Bank** banks;
    
    uint32_t refreshCounter;
    uint32_t sleepMode;
    
    int64_t actReadyTime;
    int64_t fawReadyTime[4];
    int64_t readReadyTime;
    int64_t writeReadyTime;
    int64_t wakeupReadyTime;
    
public:
    Rank(Config *_config);
    virtual ~Rank();
    
    inline const RowBuffer &getRowBuffer(Coordinates &coordinates);
    inline const int64_t getReadyTime(CommandType type, Coordinates &coordinates);
    inline int64_t getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates);
    
    friend std::ostream &operator <<(std::ostream &os, Rank &rank) {
        for (int i=0; i<(int)rank.config->nBank; ++i) 
            os << "      - bank " << i << ": " << *(rank.banks[i]) << "\n";
        return os;
    }
};

class Channel
{
protected:
    Config *config;
    
    Rank** ranks;
    
    int8_t rankSelect;
    
    int64_t anyReadyTime;
    int64_t readReadyTime;
    int64_t writeReadyTime;
    
public:
    Channel(Config *_config);
    virtual ~Channel();
    
    inline const RowBuffer &getRowBuffer(Coordinates &coordinates);
    inline const int64_t getReadyTime(CommandType type, Coordinates &coordinates);
    inline int64_t getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates);
    
    friend std::ostream &operator <<(std::ostream &os, Channel &channel) {
        for (int i=0; i<(int)channel.config->nRank; ++i) 
            os << "    - rank " << i << ":\n" << *(channel.ranks[i]);
        return os;
    }
};

class MemorySystem
{
protected:
    Config *config;
    
    Channel** channels;
    
public:
    MemorySystem(Config *_config);
    virtual ~MemorySystem();
    
    inline const RowBuffer &getRowBuffer(Coordinates &coordinates);
    inline const int64_t getReadyTime(CommandType type, Coordinates &coordinates);
    
    /** sends out the command and change the state of memory system. */
    inline int64_t getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates);
    
    friend std::ostream &operator <<(std::ostream &os, MemorySystem &system) {
        for (int i=0; i<(int)system.config->nChannel; ++i) 
            os << "  - channel " << i << ":\n" << *(system.channels[i]);
        return os;
    }
};

class MemoryController
{
protected:
    Config *config;
    
    MemorySystem states;
    
    LinkedList<Transaction> transactionQueue;
    LinkedList<Command>     commandQueue;
    LinkedList<Coordinates> openBanks;
    
    bool addCommand(int64_t clock, CommandType type, Transaction &transaction);

public:
    MemoryController(Config *_config);
    virtual ~MemoryController();
    
    bool addTransaction(uint64_t address, bool is_write);
    void cycle(int64_t clock);
};

};