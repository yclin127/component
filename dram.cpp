#include "dram.h"

using namespace DRAM;

Config::Config(std::map<std::string, int> config)
{
#define _(key) config[#key]

    nTransaction = _(transaction);
    nCommand     = _(command);
    
    nChannel = 1 << _(channel);
    nRank    = 1 << _(rank);
    nBank    = 1 << _(bank);
    
    uint8_t offset = _(line);
    mapping.channel.offset = offset; offset +=
    mapping.channel.width  = _(channel);
    mapping.column.offset  = offset; offset +=
    mapping.column.width   = _(column);
    mapping.bank.offset    = offset; offset +=
    mapping.bank.width     = _(bank);
    mapping.rank.offset    = offset; offset +=
    mapping.rank.width     = _(rank);   
    mapping.row.offset     = offset; offset +=
    mapping.row.width      = _(row);
    
    policy.max_row_idle = _(max_row_idle);
    policy.max_row_hits = _(max_row_hits);
    
    timing.transaction_delay = _(tTQ);
    timing.command_delay     = _(tCQ);
    
    timing.channel.any_to_any     = _(tCMD);
    timing.channel.act_to_any     = _(tRA);
    timing.channel.read_to_read   = _(tBL)+_(tRTRS);
    timing.channel.read_to_write  = _(tCL)+_(tBL)+_(tRTRS)-_(tCWL);
    timing.channel.write_to_read  = _(tCWL)+_(tBL)+_(tRTRS)-_(tCL);
    timing.channel.write_to_write = _(tBL)+_(tRTRS);
    
    timing.rank.act_to_act     = _(tRRD);
    timing.rank.act_to_faw     = _(tFAW);
    timing.rank.read_to_read   = std::max(_(tBL), _(tCCD));
    timing.rank.read_to_write  = _(tCL)+_(tBL)+_(tRTRS)-_(tCWL);
    timing.rank.write_to_read  = _(tCWL)+_(tBL)+_(tWTR);
    timing.rank.write_to_write = std::max(_(tBL), _(tCCD));
    timing.rank.refresh_to_act = _(tRFC);
    
    timing.bank.act_to_read   = _(tRCD)-_(tAL) + _(tRA)-_(tCMD);
    timing.bank.act_to_write  = _(tRCD)-_(tAL) + _(tRA)-_(tCMD);
    timing.bank.act_to_pre    = _(tRAS) + _(tRA)-_(tCMD);
    timing.bank.read_to_pre   = _(tAL)+_(tBL)+std::max(_(tRTP)-_(tCCD), 0);
    timing.bank.write_to_pre  = _(tAL)+_(tCWL)+_(tBL)+_(tWR);
    timing.bank.pre_to_act    = _(tRP);
    timing.bank.read_to_data  = _(tAL)+_(tCL);
    timing.bank.write_to_data = _(tAL)+_(tCWL);
    
    energy.act     = (((_(IDD0)-_(IDD3N))*_(tRAS))+((_(IDD0)-_(IDD2N))*_(tRP)))*_(devices);
    energy.read    = (_(IDD4R)-_(IDD3N))*_(tBL)*_(devices);
    energy.write   = (_(IDD4W)-_(IDD3N))*_(tBL)*_(devices);
    energy.refresh = (_(IDD5)-_(IDD3N))*_(tRFC)*_(devices);

#undef _
}

MemoryController::MemoryController(Config *_config) :
    config(_config),
    system(_config),
    transactionQueue(config->nTransaction),
    commandQueue(config->nCommand),
    bankList((int)config->nChannel*config->nRank*config->nBank)
{
}

MemoryController::~MemoryController()
{
}

bool MemoryController::addTransaction(uint64_t address, bool is_write)
{
    if (transactionQueue.is_full())
        return false;
    
    Transaction &transaction = transactionQueue.push();
    
    transaction.address     = address;
    transaction.is_write    = is_write;
    transaction.is_pending  = true;
    transaction.is_finished = false;
    transaction.readyTime   = -1;
    transaction.finishTime  = -1;
    
    /** Address mapping scheme goes here. */
    AddressMapping &mapping = config->mapping;
    
    transaction.channel  = mapping.channel.value(address);
    transaction.rank     = mapping.rank.value(address);
    transaction.bank     = mapping.bank.value(address);
    transaction.row      = mapping.row.value(address);
    transaction.column   = mapping.column.value(address);
    
    return true;
}

bool MemoryController::addCommand(int64_t clock, CommandType type, Transaction &transaction)
{
    if (commandQueue.is_full())
        return false;
    
    int64_t readyTime, finishTime;
    
    clock = clock + config->timing.command_delay;
    readyTime = std::max(clock, system.getReadyTime(type, transaction));
    /** Uncomment this line to switch from FCFS to FR-FCFS */
    if (readyTime != clock) return false;
    finishTime = system.getFinishTime(readyTime, type, transaction);
    
    Command &command = commandQueue.push();
    
    command.type        = type;
    command.readyTime   = readyTime;
    command.finishTime  = finishTime;
    command.transaction = &transaction;
    
    std::cerr << command << std::endl;
    
    return true;
}

void MemoryController::cycle(int64_t clock)
{
    Policy &policy = config->policy;
    
    LinkedList<Transaction>::Iterator itq;
    LinkedList<Command>::Iterator icq;
    LinkedList<Coordinates>::Iterator iob;
    
    transactionQueue.reset(itq);
    while (transactionQueue.next(itq)) {
        Transaction &transaction = (*itq);
        
        // transaction retirement
        if (transaction.is_finished) {
            transactionQueue.remove(itq);
        }
    }
    
    commandQueue.reset(icq);
    while (commandQueue.next(icq)) {
        Command &command = (*icq);
        
        if (command.finishTime > clock) continue;
        
        switch (command.type) {
            case COMMAND_read:
            case COMMAND_write:
            case COMMAND_read_pre:
            case COMMAND_write_pre:
                command.transaction->is_finished = true;
                command.transaction->finishTime  = clock;
                break;
                
            default:
                break;
        }
        
        commandQueue.remove(icq);
    }
    
    /** Memory scheduler algorithm goes here. */
    
    // FCFS or FR-FCFS
    transactionQueue.reset(itq);
    while (transactionQueue.next(itq)) {
        Transaction &transaction = (*itq);
        
        if (!transaction.is_pending) continue;
        
        // transaction preprocessing
        if (transaction.readyTime == -1) {
            transaction.readyTime = clock + config->timing.transaction_delay;
        }
        if (transaction.readyTime > clock) continue;
        
        RowBuffer &rowBuffer = system.getRowBuffer(transaction);
        
        // Precharge
        if (rowBuffer.tag != -1 && (rowBuffer.tag != (int32_t)transaction.row || 
            rowBuffer.hits >= policy.max_row_hits)) {
            if (!addCommand(clock, COMMAND_pre, transaction)) continue;
            assert(rowBuffer.tag == -1);
        }
        
        // Activation
        if (rowBuffer.tag == -1) {
            if (!addCommand(clock, COMMAND_act, transaction)) continue;
            assert(rowBuffer.tag == (int32_t)transaction.row);
            
            RowBuffer &rowBuffer = system.getRowBuffer(transaction);
            if (!rowBuffer.is_busy) {
                bankList.push() = (Coordinates)transaction;
                rowBuffer.is_busy = true;
            }
        }
        
        // Read / Write
        if (rowBuffer.tag == (int32_t)transaction.row) {
            CommandType type = transaction.is_write ? COMMAND_write : COMMAND_read;
            if (!addCommand(clock, type, transaction)) continue;
            transaction.is_pending = false;
        }
    }
    
    // early Precharge
    bankList.reset(iob);
    while (bankList.next(iob)) {
        Coordinates &coordinates = (*iob);
        
        RowBuffer &rowBuffer = system.getRowBuffer(coordinates);
        assert(rowBuffer.is_busy);
        // bank been precharged for another row activation.
        if (rowBuffer.tag == -1) {
            continue;
        }
        
        int64_t idleTime = system.getReadyTime(COMMAND_pre, coordinates);
        if (clock + config->timing.command_delay >= idleTime + policy.max_row_idle || 
            rowBuffer.hits >= policy.max_row_hits) {
            if (!addCommand(clock, COMMAND_pre, (Transaction &)coordinates)) continue;
            bankList.remove(iob);
            rowBuffer.is_busy = false;
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

RowBuffer &MemorySystem::getRowBuffer(Coordinates &coordinates)
{
    return channels[coordinates.channel]->getRowBuffer(coordinates);
}

int64_t MemorySystem::getReadyTime(CommandType type, Coordinates &coordinates)
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
    
    clockEnergy      = 0;
    commandBusEnergy = 0;
    addressBusEnergy = 0;
    dataBusEnergy    = 0;
}

Channel::~Channel()
{
    for (uint32_t i=0; i<config->nRank; ++i) {
        delete ranks[i];
    }
    delete [] ranks;
}

RowBuffer &Channel::getRowBuffer(Coordinates &coordinates)
{
    return ranks[coordinates.rank]->getRowBuffer(coordinates);
}

int64_t Channel::getReadyTime(CommandType type, Coordinates &coordinates)
{
    int64_t clock;
    
    switch (type) {
        case COMMAND_act:
        case COMMAND_refresh:
        case COMMAND_pre:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_pre:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            if (rankSelect != coordinates.rank) {
                clock = std::max(clock, readReadyTime);
            }
            
            return clock;
            
        case COMMAND_write:
        case COMMAND_write_pre:
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
    Energy &energy = config->energy;
    
    switch (type) {
        case COMMAND_act:
        case COMMAND_refresh:
        case COMMAND_pre:
            anyReadyTime = clock + timing.any_to_any;
            if (type == COMMAND_act) {
                anyReadyTime = std::max(anyReadyTime, clock + timing.act_to_any);
            }
            
            commandBusEnergy += energy.command_bus;
            if (type == COMMAND_act) {
                addressBusEnergy += energy.row_address_bus;
            }
            
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_read:
        case COMMAND_read_pre:
            anyReadyTime   = clock + timing.any_to_any;
            readReadyTime  = clock + timing.read_to_read;
            writeReadyTime = clock + timing.read_to_write;
            
            commandBusEnergy += energy.command_bus;
            addressBusEnergy += energy.col_address_bus;
            dataBusEnergy    += energy.data_bus;
            
            rankSelect = coordinates.rank;
            
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_write:
        case COMMAND_write_pre:
            anyReadyTime   = clock + timing.any_to_any;
            readReadyTime  = clock + timing.write_to_read;
            writeReadyTime = clock + timing.write_to_write;
            
            commandBusEnergy += energy.command_bus;
            addressBusEnergy += energy.col_address_bus;
            dataBusEnergy    += energy.data_bus;
            
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
    
    actEnergy       = 0;
    preEnergy       = 0;
    readEnergy      = 0;
    writeEnergy     = 0;
    refreshEnergy   = 0;
    powerupEnergy   = 0;
    powerdownEnergy = 0;
}

Rank::~Rank()
{
    for (uint32_t i=0; i<config->nBank; ++i) {
        delete banks[i];
    }
    delete [] banks;
}

RowBuffer &Rank::getRowBuffer(Coordinates &coordinates)
{
    return banks[coordinates.bank]->getRowBuffer(coordinates);
}

int64_t Rank::getReadyTime(CommandType type, Coordinates &coordinates)
{
    int64_t clock;
    
    switch (type) {
        case COMMAND_act:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, actReadyTime);
            clock = std::max(clock, fawReadyTime[0]);
            
            return clock;
            
        case COMMAND_pre:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_pre:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, readReadyTime);
            
            return clock;
            
        case COMMAND_write:
        case COMMAND_write_pre:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, writeReadyTime);
            
            return clock;
            
        case COMMAND_refresh:
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
    Energy &energy = config->energy;
    
    switch (type) {
        case COMMAND_act:
            actReadyTime = clock + timing.act_to_act;
            
            fawReadyTime[0] = fawReadyTime[1];
            fawReadyTime[1] = fawReadyTime[2];
            fawReadyTime[2] = fawReadyTime[3];
            fawReadyTime[3] = clock + timing.act_to_faw;
            
            actEnergy += energy.act;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_pre:
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_read:
        case COMMAND_read_pre:
            readReadyTime  = clock + timing.read_to_read;
            writeReadyTime = clock + timing.read_to_write;
            
            readEnergy += energy.read;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_write:
        case COMMAND_write_pre:
            readReadyTime  = clock + timing.write_to_read;
            writeReadyTime = clock + timing.write_to_write;
            
            writeEnergy += energy.write;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
        
        case COMMAND_refresh:
            actReadyTime = clock + timing.refresh_to_act;
            
            fawReadyTime[0] = clock + timing.refresh_to_act;
            fawReadyTime[1] = clock + timing.refresh_to_act;
            fawReadyTime[2] = clock + timing.refresh_to_act;
            fawReadyTime[3] = clock + timing.refresh_to_act;
            
            refreshEnergy += energy.refresh;
            
            return clock + timing.refresh_to_act; 
            
        default:
            assert(0);
            return -1;
    }
}

Bank::Bank(Config *_config) :
    config(_config)
{
    rowBuffer.tag = -1;
    rowBuffer.hits = 0;
    rowBuffer.is_busy = false;
    
    actReadyTime   = 0;
    preReadyTime   = -1;
    readReadyTime  = -1;
    writeReadyTime = -1;
}

Bank::~Bank()
{
}

RowBuffer &Bank::getRowBuffer(Coordinates &coordinates)
{
    return rowBuffer;
}

int64_t Bank::getReadyTime(CommandType type, Coordinates &coordinates)
{
    switch (type) {
        case COMMAND_act:
        case COMMAND_refresh:
            assert(actReadyTime != -1);
            assert(rowBuffer.tag == -1);
            
            return actReadyTime;
            
        case COMMAND_pre:
            assert(preReadyTime != -1);
            assert(rowBuffer.tag != -1);
            
            return preReadyTime;
            
        case COMMAND_read:
        case COMMAND_read_pre:
            assert(readReadyTime != -1);
            assert(rowBuffer.tag == (int32_t)coordinates.row);
            
            return readReadyTime;
            
        case COMMAND_write:
        case COMMAND_write_pre:
            assert(writeReadyTime != -1);
            assert(rowBuffer.tag == (int32_t)coordinates.row);
            
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
        case COMMAND_act:
            assert(actReadyTime != -1);
            assert(clock >= actReadyTime);
            assert(rowBuffer.tag == -1);
            
            rowBuffer.tag = (int32_t)coordinates.row;
            rowBuffer.hits = 0;
            
            actReadyTime   = -1;
            preReadyTime   = clock + timing.act_to_pre;
            readReadyTime  = clock + timing.act_to_read;
            writeReadyTime = clock + timing.act_to_write;
            
            return clock;
            
        case COMMAND_pre:
            assert(preReadyTime != -1);
            assert(clock >= preReadyTime);
            assert(rowBuffer.tag != -1);
            
            rowBuffer.tag = -1;
            
            actReadyTime   = clock + timing.pre_to_act;
            preReadyTime   = -1;
            readReadyTime  = -1;
            writeReadyTime = -1;
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_pre:
            assert(readReadyTime != -1);
            assert(clock >= readReadyTime);
            assert(rowBuffer.tag == (int32_t)coordinates.row);
            
            rowBuffer.hits += 1;
            
            if (type == COMMAND_read) {
                actReadyTime   = -1;
                preReadyTime   = std::max(preReadyTime, clock + timing.read_to_pre);
                // see rank for readReadyTime
                // see rank for writeReadyTime
            } else {
                actReadyTime   = clock + timing.read_to_pre + timing.pre_to_act;
                preReadyTime   = -1;
                readReadyTime  = -1;
                writeReadyTime = -1;
            }
            
            return clock + timing.read_to_data;
            
        case COMMAND_write:
        case COMMAND_write_pre:
            assert(writeReadyTime != -1);
            assert(clock >= writeReadyTime);
            assert(rowBuffer.tag == (int32_t)coordinates.row);
            
            rowBuffer.hits += 1;
            
            if (type == COMMAND_write) {
                actReadyTime   = -1;
                preReadyTime   = std::max(preReadyTime, clock + timing.write_to_pre);
                // see rank for readReadyTime
                // see rank for writeReadyTime
            } else {
                actReadyTime   = clock + timing.write_to_pre + timing.pre_to_act;
                preReadyTime   = -1;
                readReadyTime  = -1;
                writeReadyTime = -1;
            }
            
            return clock + timing.write_to_data;
        
        //case COMMAND_refresh:
            
        default:
            assert(0);
            return -1;
    }
}
