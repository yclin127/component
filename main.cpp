#include "dram.h"
#include <cstdlib>
#include <fstream>
#include <cstdio>
#include <cstring>

using namespace DRAM;

int main(int argc, char *argv[])
{
    
    std::map<std::string, int> config;
    
    config["transaction"] = 64;
    config["command"]     = 64;
    
    config["channel"] = 0;
    config["rank"]    = 1;
    config["bank"]    = 3;
    config["row"]     = 16;
    config["column"]  = 7;
    config["line"]    = 6;
    
    config["tCL"]   = 5;
    config["tCWL"]  = 4;
    config["tAL"]   = 0;
    config["tBL"]   = 4;
    config["tRAS"]  = 15;
    config["tRCD"]  = 5;
    config["tRRD"]  = 4;
    config["tRC"]   = 20;
    config["tRP"]   = 5;
    config["tCCD"]  = 4;
    config["tRTP"]  = 4;
    config["tWTR"]  = 4;
    config["tWR"]   = 6;
    config["tRTRS"] = 1;
    config["tRFC"]  = 64;
    config["tFAW"]  = 16;
    config["tCKE"]  = 3;
    config["tXP"]   = 3;
    
    MemoryController *mc = new MemoryController(new Config(config));
    
    FILE* file = fopen("../DRAMSim2/traces/mase_art.trc", "r");
    
    uint32_t address;
    char command[64], line[256];
    uint32_t clock, time, delay;
    bool is_write;
    clock = delay = 0;
    while(fgets(line, sizeof(line), file)) {
        sscanf(line, "0x%x %s %d", &address, command, &time);
        is_write = strcmp(command, "WRITE") == 0;
        while (clock < time) {
            mc->cycle(clock+delay);
            clock += 1;
        }
        while (!mc->addTransaction(address, is_write)) {
            mc->cycle(clock+delay);
            delay += 1;
        }
    }
    printf("delay: %d\n", delay);
    
    fclose(file);
    
    delete mc;
    
    return 0;
}
