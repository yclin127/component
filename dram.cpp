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
    
    timing.rank.powerdown_latency = new uint32_t[5];
    timing.rank.powerdown_latency[0] = _(tCKE); // double check
    timing.rank.powerdown_latency[1] = _(tCKE); // double check
    timing.rank.powerdown_latency[2] = 0;
    timing.rank.powerdown_latency[3] = _(tCKE); // double check
    timing.rank.powerdown_latency[4] = _(tCKE); // double check
    
    timing.rank.powerup_latency = new uint32_t[5];
    timing.rank.powerup_latency[0] = _(tXP); // double check
    timing.rank.powerup_latency[1] = _(tXP); // double check
    timing.rank.powerup_latency[2] = 0;
    timing.rank.powerup_latency[3] = _(tXP); // double check
    timing.rank.powerup_latency[4] = _(tXP); // double check
    
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
    
    energy.static_per_cycle = new uint32_t[6];
    energy.static_per_cycle[0] = _(IDD3N);
    energy.static_per_cycle[1] = _(IDD3Ps);
    energy.static_per_cycle[2] = _(IDD3Pf);
    energy.static_per_cycle[3] = _(IDD2N);
    energy.static_per_cycle[4] = _(IDD2Q);
    energy.static_per_cycle[5] = _(IDD2P);

#undef _
}

MemoryController::MemoryController(Config *_config) :
    config(_config),
    system(_config),
    transactionQueue(config->nTransaction),
    commandQueue(config->nCommand),
    rankList((int)config->nChannel*config->nRank),
    bankList((int)config->nChannel*config->nRank*config->nBank)
{
    Coordinates coordinates = {0};
    uint32_t refresh_step = config->timing.rank.refresh_interval/config->nRank;
    
    for (coordinates.channel=0; coordinates.channel<config->nChannel; ++coordinates.channel) {
        for (coordinates.rank=0; coordinates.rank<config->nRank; ++coordinates.rank) {                
            for (coordinates.bank=0; coordinates.bank<config->nBank; ++coordinates.bank) {
                
                // initialize row buffer
                RowBuffer &rowBuffer = system.getRowBuffer(coordinates);
                
                rowBuffer.tag = -1;
                rowBuffer.hits = 0;
                rowBuffer.is_busy = false;
            }
            
            coordinates.bank = 0;
            rankList.push() = coordinates;
            
            // initialize refresh counter
            RefreshCounter &refreshCounter = system.getRefreshCounter(coordinates);
            
            refreshCounter.busyCounter = 0;
            refreshCounter.expectedTime = refresh_step*(coordinates.rank+1);
            refreshCounter.is_sleeping = false;
        }
    }
}

MemoryController::~MemoryController()
{
}

bool MemoryController::addTransaction(int64_t clock, uint64_t address, bool is_write)
{
    if (transactionQueue.is_full())
        return false;
    
    Transaction &transaction = transactionQueue.push();
    
    transaction.address     = address;
    transaction.is_write    = is_write;
    transaction.is_pending  = true;
    transaction.is_finished = false;
    transaction.readyTime   = clock + config->timing.transaction_delay;
    transaction.finishTime  = -1;
    
    /** Address mapping scheme goes here. */
    AddressMapping &mapping = config->mapping;
    
    transaction.channel = mapping.channel.value(address);
    transaction.rank    = mapping.rank.value(address);
    transaction.bank    = mapping.bank.value(address);
    transaction.row     = mapping.row.value(address);
    transaction.column  = mapping.column.value(address);
    
    return true;
}

bool MemoryController::addCommand(int64_t clock, CommandType type, Transaction &transaction)
{
    if (commandQueue.is_full())
        return false;
    
    int64_t readyTime, finishTime;
    
    switch (type) {
        case COMMAND_act:
        case COMMAND_read:
        case COMMAND_write:
            clock = clock + config->timing.command_delay;
            break;
        default:
            break;
    }
    readyTime = std::max(clock, system.getReadyTime(type, transaction));
    /** Uncomment this line to switch from FCFS to FR-FCFS */
    if (readyTime != clock) return false;
    finishTime = system.getFinishTime(readyTime, type, transaction);
    
    Command &command = commandQueue.push();
    
    command.type        = type;
    command.readyTime   = readyTime;
    command.finishTime  = finishTime;
    command.transaction = &transaction;
    
    if (type < COMMAND_powerup) {
        std::cout << clock << " " << (
            command.type == COMMAND_act ? "act" :
            command.type == COMMAND_pre ? "pre" :
            command.type == COMMAND_read ? "read" :
            command.type == COMMAND_write ? "write" :
            command.type == COMMAND_refresh ? "refresh" :
            command.type == COMMAND_powerup ? "powerup" :
            "powerdown") 
            << " " << (int)transaction.channel 
            << " " << (int)transaction.rank 
            << " " << (int)transaction.bank;
        switch (type) {
            case COMMAND_act:
            case COMMAND_read:
            case COMMAND_write:
                std::cout << " //" 
                    << " pre: " << system.getReadyTime(COMMAND_pre, transaction)
                    << " read: " << system.getReadyTime(COMMAND_write, transaction)
                    << " write: " << system.getReadyTime(COMMAND_read, transaction);
                break;
            case COMMAND_pre:
            case COMMAND_refresh:
                std::cout << " //" 
                    << " act: " << system.getReadyTime(COMMAND_act, transaction);
                break;
            default:
                break;
        }
        std::cout << std::endl;
    }
    
    return true;
}

void MemoryController::cycle(int64_t clock)
{
    system.cycle(clock);
    
    Policy &policy = config->policy;
    
    LinkedList<Transaction>::Iterator itq;
    LinkedList<Command>::Iterator icq;
    LinkedList<Coordinates>::Iterator ico;
    
    // transaction retirement
    transactionQueue.reset(itq);
    while (transactionQueue.next(itq)) {
        Transaction &transaction = (*itq);
        
        if (transaction.is_finished) {
            transactionQueue.remove(itq);
        }
    }
    
    // comand retirement
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
        
        if (!transaction.is_pending || transaction.readyTime > clock) continue;
        
        RefreshCounter &refreshCounter = system.getRefreshCounter(transaction);
        RowBuffer &rowBuffer = system.getRowBuffer(transaction);
        
        // make way for Refresh
        if (clock >= refreshCounter.expectedTime) continue;
        
        // Power up
        if (refreshCounter.is_sleeping) {
            if (!addCommand(clock, COMMAND_powerup, transaction)) continue;
            
            refreshCounter.is_sleeping = false;
        }
        
        // Precharge
        if (rowBuffer.tag != -1 && (rowBuffer.tag != (int32_t)transaction.row || 
            rowBuffer.hits >= policy.max_row_hits)) {
            if (!addCommand(clock, COMMAND_pre, transaction)) continue;
            
            rowBuffer.tag = -1;
        }
        
        // Activation
        if (rowBuffer.tag == -1) {
            if (!addCommand(clock, COMMAND_act, transaction)) continue;
            
            rowBuffer.tag = (int32_t)transaction.row;
            rowBuffer.hits = 0;
            if (!rowBuffer.is_busy) {
                bankList.push() = (Coordinates)transaction;
                refreshCounter.busyCounter += 1;
                rowBuffer.is_busy = true;
            }
        }
        
        // Read / Write
        if (rowBuffer.tag == (int32_t)transaction.row) {
            CommandType type = transaction.is_write ? COMMAND_write : COMMAND_read;
            if (!addCommand(clock, type, transaction)) continue;
            
            rowBuffer.hits += 1;
            transaction.is_pending = false;
        }
    }
    
    // Precharge policy
    bankList.reset(ico);
    while (bankList.next(ico)) {
        Coordinates &coordinates = (*ico);
        
        RefreshCounter &refreshCounter = system.getRefreshCounter(coordinates);
        RowBuffer &rowBuffer = system.getRowBuffer(coordinates);
        
        assert(rowBuffer.is_busy);
        // bank been Precharged for another row Activation.
        if (rowBuffer.tag == -1) {
            // make way for Refresh
            if (clock >= refreshCounter.expectedTime) {
                refreshCounter.busyCounter -= 1;
                rowBuffer.is_busy = false;
                
                bankList.remove(ico);
            }
            continue;
        }
        
        int64_t readyTime = system.getReadyTime(COMMAND_pre, coordinates);
        int64_t idleTime = readyTime + policy.max_row_idle;
        
        // Precharge
        if (clock >= refreshCounter.expectedTime || // Refresh
            clock >= idleTime || rowBuffer.hits >= policy.max_row_hits // Policy
        ) {
            if (!addCommand(clock, COMMAND_pre, (Transaction &)coordinates)) continue;
            
            refreshCounter.busyCounter -= 1;
            rowBuffer.is_busy = false;
            rowBuffer.tag = -1;
            
            bankList.remove(ico);
        }
    }
    
    // Refresh policy
    rankList.reset(ico);
    while (rankList.next(ico)) {
        Coordinates &coordinates = (*ico);
        
        RefreshCounter &refreshCounter = system.getRefreshCounter(coordinates);
        
        if (clock < refreshCounter.expectedTime || refreshCounter.busyCounter > 0) continue;
        
        // Power up
        if (refreshCounter.is_sleeping) {
            if (!addCommand(clock-3, COMMAND_powerup, (Transaction &)coordinates)) continue;
            
            refreshCounter.is_sleeping = false;
        }
        
        // Refresh
        if (!addCommand(clock, COMMAND_refresh, (Transaction &)coordinates)) continue;
        
        refreshCounter.expectedTime += config->timing.rank.refresh_interval;
    }
    
    // Power down policy
    rankList.reset(ico);
    while (rankList.next(ico)) {
        Coordinates &coordinates = (*ico);
        
        RefreshCounter &refreshCounter = system.getRefreshCounter(coordinates);
        
        if (refreshCounter.is_sleeping || refreshCounter.busyCounter > 0) continue;
        
        // Power down
        if (!addCommand(clock, COMMAND_powerdown, (Transaction &)coordinates)) continue;
        
        refreshCounter.is_sleeping = true;
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

RefreshCounter &MemorySystem::getRefreshCounter(Coordinates &coordinates)
{
    return channels[coordinates.channel]->getRefreshCounter(coordinates);
}

int64_t MemorySystem::getReadyTime(CommandType type, Coordinates &coordinates)
{
    return channels[coordinates.channel]->getReadyTime(type, coordinates);
}

int64_t MemorySystem::getFinishTime(int64_t clock, CommandType type, Coordinates &coordinates)
{
    return channels[coordinates.channel]->getFinishTime(clock, type, coordinates);
}

void MemorySystem::cycle(int64_t clock)
{
    for (uint8_t channel=0; channel<config->nChannel; ++channel) {
        channels[channel]->cycle(clock);
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

RowBuffer &Channel::getRowBuffer(Coordinates &coordinates)
{
    return ranks[coordinates.rank]->getRowBuffer(coordinates);
}

RefreshCounter &Channel::getRefreshCounter(Coordinates &coordinates)
{
    return ranks[coordinates.rank]->getRefreshCounter(coordinates);
}

int64_t Channel::getReadyTime(CommandType type, Coordinates &coordinates)
{
    int64_t clock;
    
    switch (type) {
        case COMMAND_act:
        case COMMAND_pre:
        case COMMAND_refresh:
        case COMMAND_powerup:
        // case COMMAND_powerdown + n: where n indicates the n-th power down mode (starting from 0)
        case COMMAND_powerdown:
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
        case COMMAND_pre:
        case COMMAND_refresh:
        case COMMAND_powerup:
        // case COMMAND_powerdown + n: where n indicates the n-th power down mode (starting from 0)
        case COMMAND_powerdown:
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
    
    powerMode = 0;
    
    actReadyTime     = 0;
    fawReadyTime[0]  = 0;
    fawReadyTime[1]  = 0;
    fawReadyTime[2]  = 0;
    fawReadyTime[3]  = 0;
    readReadyTime    = 0;
    writeReadyTime   = 0;
    powerupReadyTime = 0;
    
    actEnergy     = 0;
    preEnergy     = 0;
    readEnergy    = 0;
    writeEnergy   = 0;
    refreshEnergy = 0;
    staticEnergy  = 0;
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

RefreshCounter &Rank::getRefreshCounter(Coordinates &coordinates)
{
    return refreshCounter;
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
            
        case COMMAND_powerup:
            assert(powerMode != 0);
            
            return powerupReadyTime;
            
        case COMMAND_refresh:
        // case COMMAND_powerdown + n: where n indicates the n-th power down mode (starting from 0)
        case COMMAND_powerdown:
            assert(powerMode == 0);
            
            clock = actReadyTime;
            for (uint8_t i=0; i<config->nBank; ++i) {
                clock = std::max(clock, banks[i]->getReadyTime(COMMAND_act, coordinates));
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
            actReadyTime = clock + timing.refresh_latency;
            
            fawReadyTime[0] = actReadyTime;
            fawReadyTime[1] = actReadyTime;
            fawReadyTime[2] = actReadyTime;
            fawReadyTime[3] = actReadyTime;
            
            refreshEnergy += energy.refresh;
            
            return clock;
            
        case COMMAND_powerup:
            actReadyTime = clock + timing.powerup_latency[powerMode - 1];
            
            fawReadyTime[0] = actReadyTime;
            fawReadyTime[1] = actReadyTime;
            fawReadyTime[2] = actReadyTime;
            fawReadyTime[3] = actReadyTime;
            
            powerupReadyTime = -1;
            
            powerMode = 0;
            
            return clock;
            
        // case COMMAND_powerdown + n: where n indicates the n-th power down mode (starting from 0)
        case COMMAND_powerdown:
            actReadyTime = -1;
            
            fawReadyTime[0] = actReadyTime;
            fawReadyTime[1] = actReadyTime;
            fawReadyTime[2] = actReadyTime;
            fawReadyTime[3] = actReadyTime;
            
            powerupReadyTime = clock + timing.powerdown_latency[type - COMMAND_powerdown];
            
            powerMode = type - COMMAND_powerup; // 0 is for power up mode
            
            return clock;
            
        default:
            assert(0);
            return -1;
    }
}

void Rank::cycle(int64_t clock)
{
    Energy &energy = config->energy;
    
    staticEnergy += energy.static_per_cycle[powerMode];
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

RowBuffer &Bank::getRowBuffer(Coordinates &coordinates)
{
    return rowBuffer;
}

int64_t Bank::getReadyTime(CommandType type, Coordinates &coordinates)
{
    switch (type) {
        case COMMAND_act:
            assert(actReadyTime != -1);
            
            return actReadyTime;
            
        case COMMAND_pre:
            assert(preReadyTime != -1);
            
            return preReadyTime;
            
        case COMMAND_read:
        case COMMAND_read_pre:
            assert(readReadyTime != -1);
            
            return readReadyTime;
            
        case COMMAND_write:
        case COMMAND_write_pre:
            assert(writeReadyTime != -1);
            
            return writeReadyTime;
            
        //case COMMAND_refresh:
        //case COMMAND_powerup:
        //case COMMAND_powerdonw+n:
            
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
            
            actReadyTime   = -1;
            preReadyTime   = clock + timing.act_to_pre;
            readReadyTime  = clock + timing.act_to_read;
            writeReadyTime = clock + timing.act_to_write;
            
            return clock;
            
        case COMMAND_pre:
            assert(preReadyTime != -1);
            assert(clock >= preReadyTime);
            
            actReadyTime   = clock + timing.pre_to_act;
            preReadyTime   = -1;
            readReadyTime  = -1;
            writeReadyTime = -1;
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_pre:
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
        case COMMAND_write_pre:
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
        //case COMMAND_powerdonw+n:
            
        default:
            assert(0);
            return -1;
    }
}
