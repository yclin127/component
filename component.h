#include <cassert>
#include <map>
#include <string>

using namespace std;

class Component {
public:
    void cycle(int clock) { assert(0) };
}

class ComponentA : public Component
{
protected:
    int var1;
    int var2;
    
public:
    int param1;
    int param2;
    int param3;
    int param4;
    
    int stat1;
    int stat2;
    
    Component(map<string, int> settings) {
        param1 = settings["param1"];
        param2 = settings["param2"];
        param3 = param1+param2;
        param4 = param1*param2;
        
        var1 = 0;
        var2 = 1;
        
        stat1 = 2;
        stat2 = 3;
    }
    
    virtual ~Component() {
    }
};
