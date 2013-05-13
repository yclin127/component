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

struct Config {    
    AddressMapping mapping;
    Timing timing;
    Energy energy;
    
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
    bool is_write;
    uint64_t address;
    /** Whether all commands of the transaction are in the command queue. */
    bool is_pending;
    /** Whether all commands of the transaction are completed. */
    bool is_finished;
    
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
    /** The time when the data command is received from a channel. */
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

class Bank
{
protected:
    Config *config;
    
    int32_t rowBuffer;
    
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
    
    inline const int32_t getRowBuffer(Coordinates &coordinates);
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
    
    inline const int32_t getRowBuffer(Coordinates &coordinates);
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
    
    inline const int32_t getRowBuffer(Coordinates &coordinates);
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
    
    inline const int32_t getRowBuffer(Coordinates &coordinates);
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
    
    bool addCommand(int64_t clock, CommandType type, Transaction &transaction);

public:
    MemoryController(Config *_config);
    virtual ~MemoryController();
    
    bool addTransaction(uint64_t address, bool is_write);
    void cycle(int64_t clock);
};

};