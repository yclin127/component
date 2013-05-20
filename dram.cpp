#include "dram.h"

using namespace DRAM;

Config::Config(std::map<std::string, int> config)
{
#define _(key) config[#key]

    nTransaction = _(transaction);
    nCommand     = _(command);
    
    nDevice  = _(devices);
    nChannel = 1 << _(channel);
    nRank    = 1 << _(rank);
    nBank    = 1 << _(bank);
    nRow     = 1 << _(row);
    nColumn  = 1 << _(column);
    
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
    timing.channel.act_to_any     = _(tRCMD);
    timing.channel.read_to_read   = _(tBL)+_(tRTRS);
    timing.channel.read_to_write  = _(tCL)+_(tBL)+_(tRTRS)-_(tCWL);
    timing.channel.write_to_read  = _(tCWL)+_(tBL)+_(tRTRS)-_(tCL);
    timing.channel.write_to_write = _(tBL)+_(tRTRS);
    
    timing.rank.act_to_act     = _(tRRD);
    timing.rank.act_to_faw     = _(tFAW);
    timing.rank.read_to_read   = std::max(_(tBL), _(tCCD));
    timing.rank.read_to_write  = _(tCL)+_(tBL)+_(tRTRS)-_(tCWL); // double check
    timing.rank.write_to_read  = _(tCWL)+_(tBL)+_(tWTR); // double check
    timing.rank.write_to_write = std::max(_(tBL), _(tCCD));
    
    timing.rank.refresh_latency  = _(tRFC);
    timing.rank.refresh_interval = _(tREFI);
    
    timing.rank.powerdown_latency = _(tCKE); // double check
    timing.rank.powerup_latency   = _(tXP); // double check
    
    timing.bank.act_to_read   = _(tRCD)-_(tAL) + _(tRCMD)-_(tCMD);
    timing.bank.act_to_write  = _(tRCD)-_(tAL) + _(tRCMD)-_(tCMD);
    timing.bank.act_to_pre    = _(tRAS) + _(tRCMD)-_(tCMD);
    timing.bank.read_to_pre   = _(tAL)+_(tBL)+std::max(_(tRTP), _(tCCD))-_(tCCD); // double check
    timing.bank.write_to_pre  = _(tAL)+_(tCWL)+_(tBL)+_(tWR); // double check
    timing.bank.pre_to_act    = _(tRP);
    timing.bank.read_to_data  = _(tAL)+_(tCL);
    timing.bank.write_to_data = _(tAL)+_(tCWL);
    
    energy.act     = (((_(IDD0)-_(IDD3N))*_(tRAS))+((_(IDD0)-_(IDD2N))*_(tRP)))*nDevice;
    energy.read    = (_(IDD4R)-_(IDD3N))*_(tBL)*nDevice;
    energy.write   = (_(IDD4W)-_(IDD3N))*_(tBL)*nDevice;
    energy.refresh = (_(IDD5)-_(IDD3N))*_(tRFC)*nDevice;
    
    energy.powerup_per_cycle   = _(IDD3N);
    energy.powerdown_per_cycle = _(IDD2Q);

#undef _
}



MemoryControllerHub::MemoryControllerHub(Config *_config) :
    config(_config),
    transactionQueue(config->nTransaction)
{
    controllers = new MemoryController*[config->nChannel];
    for (uint32_t i=0; i<config->nChannel; ++i) {
        controllers[i] = new MemoryController(config);
    }
}

MemoryControllerHub::~MemoryControllerHub()
{
    for (uint32_t i=0; i<config->nChannel; ++i) {
        delete controllers[i];
    }
    delete [] controllers;
}

bool MemoryControllerHub::addTransaction(int64_t clock, uint64_t address, bool is_write)
{
    if (transactionQueue.is_full()) return false;
    
    Transaction &transaction = transactionQueue.push();
    
    transaction.address     = address;
    transaction.is_write    = is_write;
    transaction.is_pending  = true;
    transaction.is_finished = false;
    transaction.readyTime   = clock + config->timing.transaction_delay;
    
    /** Address mapping scheme goes here. */
    AddressMapping &mapping = config->mapping;
    
    transaction.channel = mapping.channel.value(address);
    transaction.rank    = mapping.rank.value(address);
    transaction.bank    = mapping.bank.value(address);
    transaction.row     = mapping.row.value(address);
    transaction.column  = mapping.column.value(address);
    
    return true;
}

void MemoryControllerHub::cycle(int64_t clock)
{
    LinkedList<Transaction>::Iterator irq;
    
    // transaction issuing & retirement
    transactionQueue.reset(irq);
    while (transactionQueue.next(irq)) {
        Transaction &transaction = (*irq);
        
        if (transaction.is_pending) {
            if (clock >= transaction.readyTime && 
                controllers[transaction.channel]->addTransaction(&transaction)) {
                transaction.is_pending = false;
            }
        }
        else if (transaction.is_finished) {
            transactionQueue.remove(irq);
        }
    }
    
    for (uint8_t channel=0; channel<config->nChannel; ++channel) {
        controllers[channel]->cycle(clock);
    }
}



MemoryController::MemoryController(Config *_config) :
    config(_config),
    channel(_config),
    transactionQueue(config->nTransaction),
    commandQueue(config->nCommand)
{
    Coordinates coordinates = {0};
    uint32_t refresh_step = config->timing.rank.refresh_interval/config->nRank;
    
    for (coordinates.rank=0; coordinates.rank<config->nRank; ++coordinates.rank) {
        // initialize rank
        RankData &rank = channel.getRankData(coordinates);
        rank.demandCount = 0;
        rank.activeCount = 0;
        rank.refreshTime = refresh_step*(coordinates.rank+1);
        rank.is_sleeping = false;
        rank.is_busy = false;
        
        for (coordinates.bank=0; coordinates.bank<config->nBank; ++coordinates.bank) {
            // initialize bank
            BankData &bank = channel.getBankData(coordinates);
            bank.demandCount = 0;
            bank.rowBuffer = -1;
            bank.hitCount = 0;
            bank.is_busy = false;
        }
    }
}

MemoryController::~MemoryController()
{
}

bool MemoryController::addTransaction(Transaction *transaction)
{
    if (transactionQueue.is_full())
        return false;
    
    transactionQueue.push() = transaction;
    
    RankData &rank = channel.getRankData(*transaction);
    BankData &bank = channel.getBankData(*transaction);
    rank.demandCount += 1;
    bank.demandCount += 1;
    
    return true;
}

bool MemoryController::addCommand(int64_t clock, CommandType type, Transaction &transaction)
{
    if (commandQueue.is_full())
        return false;
    
    int64_t readyTime, issueTime, finishTime;
    
    readyTime = channel.getReadyTime(type, transaction);
    issueTime = clock + config->timing.command_delay;
    if (readyTime > issueTime) return false;
    
    finishTime = channel.getFinishTime(issueTime, type, transaction);
    
    Command &command = commandQueue.push();
    
    command.type        = type;
    command.issueTime   = issueTime;
    command.finishTime  = finishTime;
    command.transaction = &transaction;
    
    static const char *mne[] = {
        "act", "pre", "read", "write", "read_pre", "write_pre", 
        "refresh", "powerup", "powerdown",
    };
    if (command.type < COMMAND_powerup)
    std::cout << issueTime 
        << " " << mne[command.type]
        << " " << (int)transaction.channel 
        << " " << (int)transaction.rank 
        << " " << (command.type >= COMMAND_refresh ? 0 : (int)transaction.bank)
        << std::endl;
    
    return true;
}

void MemoryController::cycle(int64_t clock)
{
    channel.cycle(clock);
    
    Policy &policy = config->policy;
    
    Coordinates coordinates = {0};
    LinkedList<Command>::Iterator icq;
    LinkedList<Transaction *>::Iterator itq;
    
    // Command retirement
    for (commandQueue.reset(icq); commandQueue.next(icq); ) {
        Command &command = *icq;
        
        if (command.finishTime > clock) continue;
        
        switch (command.type) {
            case COMMAND_read:
            case COMMAND_write:
            case COMMAND_read_precharge:
            case COMMAND_write_precharge:
                command.transaction->is_finished = true;
                break;
                
            default:
                break;
        }
        
        commandQueue.remove(icq);
    }
    
    // Refresh policy
    for (coordinates.rank = 0; coordinates.rank < config->nRank; ++coordinates.rank) {
        RankData &rank = channel.getRankData(coordinates);
        
        if (clock < rank.refreshTime - config->timing.rank.powerup_latency) continue;
        
        rank.is_busy = true;
        
        // Power up
        if (rank.is_sleeping) {
            if (!addCommand(clock, COMMAND_powerup, (Transaction &)coordinates)) continue;
            rank.is_sleeping = false;
        }
        
        if (clock < rank.refreshTime) continue;
        
        // Precharge
        for (coordinates.bank = 0; coordinates.bank < config->nBank; ++coordinates.bank) {
            BankData &bank = channel.getBankData(coordinates);
            
            if (bank.rowBuffer != -1) {
                if (!addCommand(clock, COMMAND_precharge, (Transaction &)coordinates)) continue;
                rank.activeCount -= 1;
                bank.rowBuffer = -1;
            }
        }
        if (rank.activeCount > 0) continue;
        
        // Refresh
        if (!addCommand(clock, COMMAND_refresh, (Transaction &)coordinates)) continue;
        rank.refreshTime += config->timing.rank.refresh_interval;
        rank.is_busy = false;
    }
    
    // Schedule policy
    for (transactionQueue.reset(itq); transactionQueue.next(itq); ) {
        Transaction &transaction = **itq;
        RankData &rank = channel.getRankData(transaction);
        BankData &bank = channel.getBankData(transaction);
        
        // make way for Refresh
        if (clock >= rank.refreshTime) continue;
        
        rank.is_busy = true;
        
        // Power up
        if (rank.is_sleeping) {
            if (!addCommand(clock, COMMAND_powerup, transaction)) continue;
            rank.is_sleeping = false;
        }
        
        // Precharge
        if (bank.rowBuffer != -1 && (bank.rowBuffer != (int)transaction.row || bank.hitCount >= policy.max_row_hits)) {
            if (!addCommand(clock, COMMAND_precharge, transaction)) continue;
            rank.activeCount -= 1;
            bank.rowBuffer = -1;
        }
        
        // Activate
        if (bank.rowBuffer == -1) {
            if (!addCommand(clock, COMMAND_activate, transaction)) continue;
            rank.activeCount += 1;
            bank.rowBuffer = transaction.row;
            bank.hitCount = 0;
        }
        
        // Read / Write
        CommandType type = transaction.is_write ? COMMAND_write : COMMAND_read;
        if (!addCommand(clock, type, transaction)) continue;
        rank.demandCount -= 1;
        bank.demandCount -= 1;
        bank.hitCount += 1;
        transactionQueue.remove(itq);
    }

    // Precharge policy
    for (coordinates.rank = 0; coordinates.rank < config->nRank; ++coordinates.rank) {
        RankData &rank = channel.getRankData(coordinates);
        for (coordinates.bank = 0; coordinates.bank < config->nBank; ++coordinates.bank) {
            BankData &bank = channel.getBankData(coordinates);
            
            if (bank.rowBuffer == -1 || bank.demandCount > 0) continue;
            
            int64_t idleTime = clock - policy.max_row_idle;
            if (!addCommand(idleTime, COMMAND_precharge, (Transaction &)coordinates)) continue;
            rank.activeCount -= 1;
            bank.rowBuffer = -1;
            
            if (rank.demandCount == 0 && rank.activeCount == 0) {
                rank.is_busy = false;
            }
        }
    }
    
    // Power down policy
    for (coordinates.rank = 0; coordinates.rank < config->nRank; ++coordinates.rank) {
        RankData &rank = channel.getRankData(coordinates);
        
        if (rank.is_sleeping || rank.is_busy) continue;
        
        // Power down
        if (!addCommand(clock, COMMAND_powerdown, (Transaction &)coordinates)) continue;
        rank.is_sleeping = true;
    }
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

BankData &Channel::getBankData(Coordinates &coordinates)
{
    return ranks[coordinates.rank]->getBankData(coordinates);
}

RankData &Channel::getRankData(Coordinates &coordinates)
{
    return ranks[coordinates.rank]->getRankData(coordinates);
}

int64_t Channel::getReadyTime(CommandType type, Coordinates &coordinates)
{
    int64_t clock;
    
    switch (type) {
        case COMMAND_activate:
        case COMMAND_precharge:
        case COMMAND_refresh:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            if (rankSelect != coordinates.rank) {
                clock = std::max(clock, readReadyTime);
            }
            
            return clock;
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            if (rankSelect != coordinates.rank) {
                clock = std::max(clock, writeReadyTime);
            }
            
            return clock;
            
        case COMMAND_powerup:
        case COMMAND_powerdown:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            
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
        case COMMAND_activate:
        case COMMAND_precharge:
        case COMMAND_refresh:
            anyReadyTime = clock + timing.any_to_any;
            if (type == COMMAND_activate) {
                anyReadyTime = std::max(anyReadyTime, clock + timing.act_to_any);
            }
            
            commandBusEnergy += energy.command_bus;
            if (type == COMMAND_activate) {
                addressBusEnergy += energy.row_address_bus;
            }
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            anyReadyTime   = clock + timing.any_to_any;
            readReadyTime  = clock + timing.read_to_read;
            writeReadyTime = clock + timing.read_to_write;
            
            commandBusEnergy += energy.command_bus;
            addressBusEnergy += energy.col_address_bus;
            dataBusEnergy    += energy.data_bus;
            
            rankSelect = coordinates.rank;
            
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            anyReadyTime   = clock + timing.any_to_any;
            readReadyTime  = clock + timing.write_to_read;
            writeReadyTime = clock + timing.write_to_write;
            
            commandBusEnergy += energy.command_bus;
            addressBusEnergy += energy.col_address_bus;
            dataBusEnergy    += energy.data_bus;
            
            rankSelect = coordinates.rank;
            
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_powerup:
        case COMMAND_powerdown:
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        default:
            assert(0);
            return -1;
    }
}

void Channel::cycle(int64_t clock)
{
    Energy &energy = config->energy;
    
    clockEnergy += energy.clock_per_cycle;
    
    for (uint8_t rank=0; rank<config->nRank; ++rank) {
        ranks[rank]->cycle(clock);
    }
}

Rank::Rank(Config *_config) :
    config(_config)
{
    banks = new Bank*[config->nBank];
    for (uint32_t i=0; i<config->nBank; ++i) {
        banks[i] = new Bank(config);
    }
    
    actReadyTime     = 0;
    fawReadyTime[0]  = 0;
    fawReadyTime[1]  = 0;
    fawReadyTime[2]  = 0;
    fawReadyTime[3]  = 0;
    readReadyTime    = 0;
    writeReadyTime   = 0;
    powerupReadyTime = -1;
    
    actEnergy        = 0;
    preEnergy        = 0;
    readEnergy       = 0;
    writeEnergy      = 0;
    refreshEnergy    = 0;
    backgroundEnergy = 0;
}

Rank::~Rank()
{
    for (uint32_t i=0; i<config->nBank; ++i) {
        delete banks[i];
    }
    delete [] banks;
}

BankData &Rank::getBankData(Coordinates &coordinates)
{
    return banks[coordinates.bank]->getBankData(coordinates);
}

RankData &Rank::getRankData(Coordinates &coordinates)
{
    return data;
}

int64_t Rank::getReadyTime(CommandType type, Coordinates &coordinates)
{
    int64_t clock;
    
    switch (type) {
        case COMMAND_activate:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, actReadyTime);
            clock = std::max(clock, fawReadyTime[0]);
            
            return clock;
            
        case COMMAND_precharge:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, readReadyTime);
            
            return clock;
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, writeReadyTime);
            
            return clock;
            
        case COMMAND_refresh:
            clock = actReadyTime;
            for (uint8_t i=0; i<config->nBank; ++i) {
                clock = std::max(clock, banks[i]->getReadyTime(COMMAND_activate, coordinates));
            }
            
            return clock;
            
        case COMMAND_powerup:
            return powerupReadyTime;
            
        case COMMAND_powerdown:
            return 0;
            
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
        case COMMAND_activate:
            actReadyTime = clock + timing.act_to_act;
            
            fawReadyTime[0] = fawReadyTime[1];
            fawReadyTime[1] = fawReadyTime[2];
            fawReadyTime[2] = fawReadyTime[3];
            fawReadyTime[3] = clock + timing.act_to_faw;
            
            actEnergy += energy.act;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_precharge:
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            readReadyTime  = clock + timing.read_to_read;
            writeReadyTime = clock + timing.read_to_write;
            
            readEnergy += energy.read;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            readReadyTime  = clock + timing.write_to_read;
            writeReadyTime = clock + timing.write_to_write;
            
            writeEnergy += energy.write;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
        
        case COMMAND_refresh:
            actReadyTime = clock + timing.refresh_latency;
            
            fawReadyTime[0] = actReadyTime;
            fawReadyTime[1] = actReadyTime;
            fawReadyTime[2] = actReadyTime;
            fawReadyTime[3] = actReadyTime;
            
            refreshEnergy += energy.refresh;
            
            return clock;
            
        case COMMAND_powerup:
            actReadyTime = clock + timing.powerup_latency;
            
            fawReadyTime[0] = actReadyTime;
            fawReadyTime[1] = actReadyTime;
            fawReadyTime[2] = actReadyTime;
            fawReadyTime[3] = actReadyTime;
            
            powerupReadyTime = -1;
            
            return clock;
            
        case COMMAND_powerdown:
            actReadyTime = -1;
            
            fawReadyTime[0] = actReadyTime;
            fawReadyTime[1] = actReadyTime;
            fawReadyTime[2] = actReadyTime;
            fawReadyTime[3] = actReadyTime;
            
            powerupReadyTime = clock + timing.powerdown_latency;
            
            return clock;
            
        default:
            assert(0);
            return -1;
    }
}

void Rank::cycle(int64_t clock)
{
    Energy &energy = config->energy;
    
    if (powerupReadyTime == -1)
        backgroundEnergy += energy.powerup_per_cycle;
    else
        backgroundEnergy += energy.powerdown_per_cycle;
}

Bank::Bank(Config *_config) :
    config(_config)
{
    actReadyTime   = 0;
    preReadyTime   = -1;
    readReadyTime  = -1;
    writeReadyTime = -1;
}

Bank::~Bank()
{
}

BankData &Bank::getBankData(Coordinates &coordinates)
{
    return data;
}

int64_t Bank::getReadyTime(CommandType type, Coordinates &coordinates)
{
    switch (type) {
        case COMMAND_activate:
            assert(actReadyTime != -1);
            
            return actReadyTime;
            
        case COMMAND_precharge:
            assert(preReadyTime != -1);
            
            return preReadyTime;
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            assert(readReadyTime != -1);
            
            return readReadyTime;
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            assert(writeReadyTime != -1);
            
            return writeReadyTime;
            
        //case COMMAND_refresh:
        //case COMMAND_powerup:
        //case COMMAND_powerdonw:
            
        default:
            assert(0);
            return -1;
    }
}

int64_t Bank::getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates)
{
    BankTiming &timing = config->timing.bank;
    
    switch (type) {
        case COMMAND_activate:
            assert(actReadyTime != -1);
            assert(clock >= actReadyTime);
            
            actReadyTime   = -1;
            preReadyTime   = clock + timing.act_to_pre;
            readReadyTime  = clock + timing.act_to_read;
            writeReadyTime = clock + timing.act_to_write;
            
            return clock;
            
        case COMMAND_precharge:
            assert(preReadyTime != -1);
            assert(clock >= preReadyTime);
            
            actReadyTime   = clock + timing.pre_to_act;
            preReadyTime   = -1;
            readReadyTime  = -1;
            writeReadyTime = -1;
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            assert(readReadyTime != -1);
            assert(clock >= readReadyTime);
            
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
        case COMMAND_write_precharge:
            assert(writeReadyTime != -1);
            assert(clock >= writeReadyTime);
            
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
        //case COMMAND_powerup:
        //case COMMAND_powerdonw:
            
        default:
            assert(0);
            return -1;
    }
}
