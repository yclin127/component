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
    uint32_t any_to_any;
    uint32_t act_to_any;
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
    
    uint32_t refresh_latency;
    uint32_t refresh_interval;
    
    uint32_t powerdown_latency;
    uint32_t powerup_latency;
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
    uint32_t clock_per_cycle;
    uint32_t command_bus;
    uint32_t row_address_bus;
    uint32_t col_address_bus;
    uint32_t data_bus;
    
    uint32_t act;
    uint32_t read;
    uint32_t write;
    uint32_t refresh;
    
    uint32_t powerup_per_cycle;
    uint32_t powerdown_per_cycle;
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
    
    uint32_t nDevice;
    uint32_t nChannel;
    uint32_t nRank;
    uint32_t nBank;
    uint32_t nRow;
    uint32_t nColumn;
    
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

struct Transaction : public Coordinates {
    uint64_t address;
    bool is_write;
    bool is_pending;
    bool is_finished;
    int64_t readyTime; /**< The time when the transaction is ready for dispatching. */
    
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
    COMMAND_activate, /**< row activation */
    COMMAND_precharge, /**< row precharge */
    COMMAND_read, /**< column read */
    COMMAND_write, /**< column write */
    COMMAND_read_precharge, /**< column read with auto row precharge */
    COMMAND_write_precharge, /**< column write with auto row precharge */
    COMMAND_refresh, /**< rank refresh */
    COMMAND_powerup, /**< rank powerup */
    COMMAND_powerdown, /**< rank powerdown */
};

/** DRAM command */
struct Command {
    CommandType type; /**< DRAM command type */
    int64_t issueTime; /**< The time when the command is sent through a channel. */
    int64_t finishTime; /**< The time when the data is received from or send through a channel. */
    Transaction *transaction; /**< The cooresponding transaction of the command. */
    
    friend std::ostream &operator <<(std::ostream &os, Command &command) {
        os << "{"
           << "type: " << command.type 
           << ", issueTime: " << command.issueTime 
           << ", finishTime: " << command.finishTime 
           << ", transaction: " << *(command.transaction)
           << "}";
        return os;
    }
};



struct BankData {
    int32_t demandCount;
    int32_t rowBuffer;
    uint8_t hitCount;
    bool is_busy;
};

struct RankData {
    int32_t demandCount;
    int32_t activeCount;
    int32_t refreshTime;
    bool is_sleeping;
    bool is_busy;
};



class Bank
{
protected:
    Config *config;
    
    BankData data;
    
    int64_t actReadyTime;
    int64_t preReadyTime;
    int64_t readReadyTime;
    int64_t writeReadyTime;
    
public:
    Bank(Config *_config);    
    virtual ~Bank();
    
    inline BankData &getBankData(Coordinates &coordinates);
    inline int64_t getReadyTime(CommandType type, Coordinates &coordinates);
    inline int64_t getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates);
};

class Rank
{
protected:
    Config *config;
    
    Bank** banks;
    
    RankData data;
    
    int64_t actReadyTime;
    int64_t fawReadyTime[4];
    int64_t readReadyTime;
    int64_t writeReadyTime;
    int64_t powerupReadyTime;
    
    uint64_t actEnergy;
    uint64_t preEnergy;
    uint64_t readEnergy;
    uint64_t writeEnergy;
    uint64_t refreshEnergy;
    uint64_t backgroundEnergy;
    
public:
    Rank(Config *_config);
    virtual ~Rank();
    
    inline BankData &getBankData(Coordinates &coordinates);
    inline RankData &getRankData(Coordinates &coordinates);
    inline int64_t getReadyTime(CommandType type, Coordinates &coordinates);
    inline int64_t getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates);
    
    inline void cycle(int64_t clock);
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
    
    uint64_t clockEnergy;
    uint64_t commandBusEnergy;
    uint64_t addressBusEnergy;
    uint64_t dataBusEnergy;
    
public:
    Channel(Config *_config);
    virtual ~Channel();
    
    inline BankData &getBankData(Coordinates &coordinates);
    inline RankData &getRankData(Coordinates &coordinates);
    inline int64_t getReadyTime(CommandType type, Coordinates &coordinates);
    inline int64_t getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates);
    
    inline void cycle(int64_t clock);
};

class MemoryController
{
protected:
    Config *config;
    
    Channel channel;
    
    LinkedList<Transaction*> transactionQueue;
    LinkedList<Command>      commandQueue;
    
    bool addCommand(int64_t clock, CommandType type, Transaction &transaction);

public:
    MemoryController(Config *_config);
    virtual ~MemoryController();
    
    bool addTransaction(Transaction *transaction);
    void cycle(int64_t clock);
};

class MemoryControllerHub
{
protected:
    Config *config;
    
    MemoryController** controllers;
    
    LinkedList<Transaction> transactionQueue;

public:
    MemoryControllerHub(Config *_config);
    virtual ~MemoryControllerHub();
    
    bool addTransaction(int64_t clock, uint64_t address, bool is_write);
    void cycle(int64_t clock);
};

};