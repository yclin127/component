#include "dram.h"
#include <iostream>

using namespace DRAM;

Config::Config(std::map<std::string, int> config)
{
    nTransaction = config["transaction"];
    nCommand     = config["command"];
    
    nChannel = 1 << config["channel"];
    nRank    = 1 << config["rank"];
    nBank    = 1 << config["bank"];
    
    uint8_t offset = config["line"];
    mapping.channel.offset = offset; offset +=
    mapping.channel.width  = config["channel"];
    mapping.column.offset  = offset; offset +=
    mapping.column.width   = config["column"];
    mapping.bank.offset    = offset; offset +=
    mapping.bank.width     = config["bank"];
    mapping.rank.offset    = offset; offset +=
    mapping.rank.width     = config["rank"];   
    mapping.row.offset     = offset; offset +=
    mapping.row.width      = config["row"];
    
    timing.channel.read_to_read   = config["tBL"]+config["tRTRS"];
    timing.channel.read_to_write  = config["tCL"]+config["tBL"]+config["tRTRS"]-config["tCWL"];
    timing.channel.write_to_read  = config["tCWL"]+config["tBL"]+config["tRTRS"]-config["tCAS"];
    timing.channel.write_to_write = config["tBL"]+config["tOST"];
    
    timing.rank.act_to_act     = config["tRRD"];
    timing.rank.act_to_faw     = config["tFAW"];
    timing.rank.read_to_read   = std::max(config["tBL"], config["tCCD"]);
    timing.rank.read_to_write  = config["tCL"]+config["tBL"]+config["tRTRS"]-config["tCWL"];
    timing.rank.write_to_read  = config["tCWL"]+config["tBL"]+config["tWTR"];
    timing.rank.write_to_write = std::max(config["tBL"], config["tCCD"]);
    timing.rank.refresh_to_act = config["tRFC"];
    
    timing.bank.act_to_read   = config["tRCD"]-config["tAL"];
    timing.bank.act_to_write  = config["tRCD"]-config["tAL"];
    timing.bank.act_to_pre    = config["tRAS"];
    timing.bank.read_to_pre   = config["tAL"]+config["tBL"]+std::max(config["tRTP"]-config["tCCD"], 0);
    timing.bank.write_to_pre  = config["tAL"]+config["tCWL"]+config["tBL"]+config["tWR"];
    timing.bank.pre_to_act    = config["tRP"];
    timing.bank.read_to_data  = config["tAL"]+config["tCL"];
    timing.bank.write_to_data = config["tAL"]+config["tCWL"];
}

MemoryController::MemoryController(Config *_config) :
    config(_config),
    states(_config),
    transactionQueue(config->nTransaction),
    commandQueue(config->nCommand)
{
}

MemoryController::~MemoryController()
{
    std::cerr << "statistic:\n" << states << std::endl;
}

bool MemoryController::addCommand(int64_t clock, CommandType type, Transaction &transaction)
{
    if (commandQueue.is_full())
        return false;
    
    Command &command = commandQueue.enque();
    
    command.type        = type;
    command.readyTime   = std::max(clock, states.getReadyTime(command.type, transaction));
    command.finishTime  = states.getFinishTime(command.readyTime, command.type, transaction);
    command.transaction = &transaction;
    
    std::cerr << command << std::endl;
    
    return true;
}

bool MemoryController::addTransaction(uint64_t address, bool is_write)
{
    if (transactionQueue.is_full())
        return false;
    
    Transaction &transaction = transactionQueue.enque();
    AddressMapping &mapping = config->mapping;
    
    transaction.is_write = is_write;
    transaction.address  = address;
    transaction.channel  = mapping.channel.value(address);
    transaction.rank     = mapping.rank.value(address);
    transaction.bank     = mapping.bank.value(address);
    transaction.row      = mapping.row.value(address);
    transaction.column   = mapping.column.value(address);
    
    transaction.is_pending  = true;
    transaction.is_finished = false;
    
    return true;
}

void MemoryController::cycle(int64_t clock)
{
    LinkedList<Transaction>::Iterator itq;
    LinkedList<Command>::Iterator icq;
    
    // FCFS or FR-FCFS
    transactionQueue.reset(itq);
    while (transactionQueue.next(itq)) {
        Transaction &transaction = (*itq);
        
        if (!transaction.is_pending) continue;
        
        int64_t rowBuffer = states.getRowBuffer(transaction);
        
        if (rowBuffer != -1 && rowBuffer != transaction.row) {
            if (!addCommand(clock, COMMAND_PRE, transaction)) continue;
            rowBuffer = -1; // getRowBuffer(transaction);
        }
        
        if (rowBuffer == -1) {
            if (!addCommand(clock, COMMAND_ACT, transaction)) continue;
            rowBuffer = transaction.row; // getRowBuffer(transaction);
        }
        
        if (rowBuffer == transaction.row) {
            CommandType type = transaction.is_write ? COMMAND_WRITE : COMMAND_READ;
            if (!addCommand(clock, type, transaction)) continue;
            transaction.is_pending = false;
        }
    }
    
    commandQueue.reset(icq);
    while (commandQueue.next(icq)) {
        Command &command = (*icq);
        
        if (command.finishTime > clock) continue;
        
        switch (command.type) {
            case COMMAND_READ:
            case COMMAND_WRITE:
            case COMMAND_READ_PRE:
            case COMMAND_WRITE_PRE:
                command.transaction->is_finished = true;
                break;
                
            default:
                break;
        }
        
        commandQueue.remove(icq);
    }
    
    transactionQueue.reset(itq);
    while (transactionQueue.next(itq)) {
        Transaction &transaction = (*itq);
        
        if (transaction.is_finished) {
            transactionQueue.remove(itq);
        }
    }
}

MemorySystem::MemorySystem(Config *_config) :
    config(_config)
{
    channels = new Channel*[config->nChannel];
    for (uint32_t i=0; i<config->nChannel; ++i) {
        channels[i] = new Channel(config);
    }
}

MemorySystem::~MemorySystem()
{
    for (uint32_t i=0; i<config->nChannel; ++i) {
        delete channels[i];
    }
    delete [] channels;
}

const int32_t MemorySystem::getRowBuffer(Coordinates &coordinates)
{
    return channels[coordinates.channel]->getRowBuffer(coordinates);
}

const int64_t MemorySystem::getReadyTime(CommandType type, Coordinates &coordinates)
{
    return channels[coordinates.channel]->getReadyTime(type, coordinates);
}

int64_t MemorySystem::getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates)
{
    return channels[coordinates.channel]->getFinishTime(clock, type, coordinates);
}

Channel::Channel(Config *_config) :
    config(_config)
{
    ranks = new Rank*[config->nRank];
    for (uint32_t i=0; i<config->nRank; ++i) {
        ranks[i] = new Rank(config);
    }
    
    rankSelect = -1;
    
    anyReadyTime   = 0;
    readReadyTime  = 0;
    writeReadyTime = 0;
}

Channel::~Channel()
{
    for (uint32_t i=0; i<config->nRank; ++i) {
        delete ranks[i];
    }
    delete [] ranks;
}

const int32_t Channel::getRowBuffer(Coordinates &coordinates)
{
    return ranks[coordinates.rank]->getRowBuffer(coordinates);
}

const int64_t Channel::getReadyTime(CommandType type, Coordinates &coordinates)
{
    int64_t clock;
    
    switch (type) {
        case COMMAND_ACT:
        case COMMAND_REFRESH:
        case COMMAND_PRE:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            
            return clock;
            
        case COMMAND_READ:
        case COMMAND_READ_PRE:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            if (rankSelect != coordinates.rank) {
                clock = std::max(clock, readReadyTime);
            }
            
            return clock;
            
        case COMMAND_WRITE:
        case COMMAND_WRITE_PRE:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            if (rankSelect != coordinates.rank) {
                clock = std::max(clock, writeReadyTime);
            }
            
            return clock;
            
        default:
            assert(0);
            return -1;
    }
}

int64_t Channel::getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates)
{
    ChannelTiming &timing = config->timing.channel;
    
    switch (type) {
        case COMMAND_ACT:
        case COMMAND_REFRESH:
        case COMMAND_PRE:
            anyReadyTime   = clock + 1;
            
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_READ:
        case COMMAND_READ_PRE:
            anyReadyTime   = clock + 1;
            readReadyTime  = clock + timing.read_to_read;
            writeReadyTime = clock + timing.read_to_write;
            
            rankSelect = coordinates.rank;
            
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_WRITE:
        case COMMAND_WRITE_PRE:
            anyReadyTime   = clock + 1;
            readReadyTime  = clock + timing.write_to_read;
            writeReadyTime = clock + timing.write_to_write;
            
            rankSelect = coordinates.rank;
            
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        default:
            assert(0);
            return -1;
    }
}

Rank::Rank(Config *_config) :
    config(_config)
{
    banks = new Bank*[config->nBank];
    for (uint32_t i=0; i<config->nBank; ++i) {
        banks[i] = new Bank(config);
    }
    
    refreshCounter = 0;
    sleepMode      = 0;
    
    actReadyTime     = 0;
    fawReadyTime[0]  = 0;
    fawReadyTime[1]  = 0;
    fawReadyTime[2]  = 0;
    fawReadyTime[3]  = 0;
    readReadyTime    = 0;
    writeReadyTime   = 0;
    wakeupReadyTime  = 0;
}

Rank::~Rank()
{
    for (uint32_t i=0; i<config->nBank; ++i) {
        delete banks[i];
    }
    delete [] banks;
}

const int32_t Rank::getRowBuffer(Coordinates &coordinates)
{
    return banks[coordinates.bank]->getRowBuffer(coordinates);
}

const int64_t Rank::getReadyTime(CommandType type, Coordinates &coordinates)
{
    int64_t clock;
    
    switch (type) {
        case COMMAND_ACT:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, actReadyTime);
            clock = std::max(clock, fawReadyTime[0]);
            
            return clock;
            
        case COMMAND_PRE:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            
            return clock;
            
        case COMMAND_READ:
        case COMMAND_READ_PRE:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, readReadyTime);
            
            return clock;
            
        case COMMAND_WRITE:
        case COMMAND_WRITE_PRE:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, writeReadyTime);
            
            return clock;
            
        case COMMAND_REFRESH:
            clock = actReadyTime;
            for (uint8_t i=0; i<config->nBank; ++i) {
                clock = std::max(clock, banks[i]->getReadyTime(type, coordinates));
            }
            
            return clock;
            
        default:
            assert(0);
            return -1;
    }
}

int64_t Rank::getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates)
{
    RankTiming &timing = config->timing.rank;
    
    switch (type) {
        case COMMAND_ACT:
            actReadyTime = clock + timing.act_to_act;
            
            fawReadyTime[0] = fawReadyTime[1];
            fawReadyTime[1] = fawReadyTime[2];
            fawReadyTime[2] = fawReadyTime[3];
            fawReadyTime[3] = clock + timing.act_to_faw;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_PRE:
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_READ:
        case COMMAND_READ_PRE:
            readReadyTime  = clock + timing.read_to_read;
            writeReadyTime = clock + timing.read_to_write;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_WRITE:
        case COMMAND_WRITE_PRE:
            readReadyTime  = clock + timing.write_to_read;
            writeReadyTime = clock + timing.write_to_write;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
        
        case COMMAND_REFRESH:
            actReadyTime = clock + timing.refresh_to_act;
            
            fawReadyTime[0] = clock + timing.refresh_to_act;
            fawReadyTime[1] = clock + timing.refresh_to_act;
            fawReadyTime[2] = clock + timing.refresh_to_act;
            fawReadyTime[3] = clock + timing.refresh_to_act;
            
            return clock + timing.refresh_to_act; 
            
        default:
            assert(0);
            return -1;
    }
}

Bank::Bank(Config *_config) :
    config(_config)
{
    rowBuffer = -1;
    
    actReadyTime   = 0;
    preReadyTime   = -1;
    readReadyTime  = -1;
    writeReadyTime = -1;
    
    
    actCount   = 0;
    preCount   = 0;
    readCount  = 0;
    writeCount = 0;
}

Bank::~Bank()
{
}

const int32_t Bank::getRowBuffer(Coordinates &coordinates)
{
    return rowBuffer;
}

const int64_t Bank::getReadyTime(CommandType type, Coordinates &coordinates)
{
    switch (type) {
        case COMMAND_ACT:
        case COMMAND_REFRESH:
            assert(actReadyTime != -1);
            assert(rowBuffer == -1);
            
            return actReadyTime;
            
        case COMMAND_PRE:
            assert(preReadyTime != -1);
            assert(rowBuffer != -1);
            
            return preReadyTime;
            
        case COMMAND_READ:
        case COMMAND_READ_PRE:
            assert(readReadyTime != -1);
            assert(rowBuffer == (int32_t)coordinates.row);
            
            return readReadyTime;
            
        case COMMAND_WRITE:
        case COMMAND_WRITE_PRE:
            assert(writeReadyTime != -1);
            assert(rowBuffer == (int32_t)coordinates.row);
            
            return writeReadyTime;
            
        default:
            assert(0);
            return -1;
    }
}

int64_t Bank::getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates)
{
    BankTiming &timing = config->timing.bank;
    
    switch (type) {
        case COMMAND_ACT:
            assert(actReadyTime != -1);
            assert(clock >= actReadyTime);
            assert(rowBuffer == -1);
            
            rowBuffer = coordinates.row;
            
            actReadyTime   = -1;
            preReadyTime   = clock + timing.act_to_pre;
            readReadyTime  = clock + timing.act_to_read;
            writeReadyTime = clock + timing.act_to_write;
            
            actCount += 1;
            
            return clock;
            
        case COMMAND_PRE:
            assert(preReadyTime != -1);
            assert(clock >= preReadyTime);
            assert(rowBuffer != -1);
            
            rowBuffer = -1;
            
            actReadyTime   = clock + timing.pre_to_act;
            preReadyTime   = -1;
            readReadyTime  = -1;
            writeReadyTime = -1;
            
            preCount += 1;
            
            return clock;
            
        case COMMAND_READ:
        case COMMAND_READ_PRE:
            assert(readReadyTime != -1);
            assert(clock >= readReadyTime);
            assert(rowBuffer == (int32_t)coordinates.row);
            
            if (type == COMMAND_READ) {
                actReadyTime   = -1;
                preReadyTime   = clock + timing.read_to_pre;
                readReadyTime  = clock; // delay is on rank level
                writeReadyTime = clock; // delay is on rank level
            } else {
                actReadyTime   = clock + timing.read_to_pre + timing.pre_to_act;
                preReadyTime   = -1;
                readReadyTime  = -1;
                writeReadyTime = -1;
                
                preCount += 1;
            }
            
            readCount += 1;
            
            return clock + timing.read_to_data;
            
        case COMMAND_WRITE:
        case COMMAND_WRITE_PRE:
            assert(writeReadyTime != -1);
            assert(clock >= writeReadyTime);
            assert(rowBuffer == (int32_t)coordinates.row);
            
            if (type == COMMAND_WRITE) {
                actReadyTime   = -1;
                preReadyTime   = clock + timing.write_to_pre;
                readReadyTime  = clock; // delay is on rank level
                writeReadyTime = clock; // delay is on rank level
            } else {
                actReadyTime   = clock + timing.write_to_pre + timing.pre_to_act;
                preReadyTime   = -1;
                readReadyTime  = -1;
                writeReadyTime = -1;
                
                preCount += 1;
            }
            
            writeCount += 1;
            
            return clock + timing.write_to_data;
        
        //case COMMAND_REFRESH:
            
        default:
            assert(0);
            return -1;
    }
}
