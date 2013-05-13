#include "dram.h"
#include <cstdlib>
#include <fstream>
#include <cstdio>
#include <cstring>

using namespace DRAM;

int main(int argc, char *argv[])
{
    std::map<std::string, int> settings;
    
    settings["transaction"] = 64;
    settings["command"]     = 64;
    
    settings["channel"] = 0;
    settings["rank"]    = 1;
    settings["bank"]    = 3;
    settings["row"]     = 16;
    settings["column"]  = 7;
    settings["line"]    = 6;
    
    settings["tTQ"]   = 1;
    settings["tCQ"]   = 2;
    
    settings["tCL"]   = 5;
    settings["tCWL"]  = 4;
    settings["tAL"]   = 0;
    settings["tBL"]   = 4;
    settings["tRAS"]  = 15;
    settings["tRCD"]  = 5;
    settings["tRRD"]  = 4;
    settings["tRC"]   = 20;
    settings["tRP"]   = 5;
    settings["tCCD"]  = 4;
    settings["tRTP"]  = 4;
    settings["tWTR"]  = 4;
    settings["tWR"]   = 6;
    settings["tRTRS"] = 1;
    settings["tRFC"]  = 64;
    settings["tFAW"]  = 16;
    settings["tCKE"]  = 3;
    settings["tXP"]   = 3;
    
    Config *config = new Config(settings);    
    MemoryController *mc = new MemoryController(config);
    
    FILE* file = fopen("../DRAMSim2/traces/mase_art.trc", "r");
    
    uint32_t address;
    char command[64], line[256];
    uint32_t clock, time;
    bool is_write;
    clock = 0;
    while(fgets(line, sizeof(line), file) && clock < 1000) {
        sscanf(line, "0x%x %s %d", &address, command, &time);
        is_write = strcmp(command, "WRITE") == 0;
        while (clock < time) {
            mc->cycle(clock);
            clock += 1;
        }
        while (!mc->addTransaction(address, is_write)) {
            mc->cycle(clock);
            clock += 1;
        }
    }
    
    fclose(file);
    
    delete mc;
    delete config;
    
    return 0;
}
