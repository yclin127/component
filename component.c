#include <string.h>
#include <stdio.h>

struct field_t {
    char *name;
    int *pointer;
};
struct assign_t {
    char *name;
    int pointer;
};

struct component_t
{
    static const field_t __fields[];
    static bool __configured;
    static void configure(assign_t *settings);
    
    static int param1;
    static int param2;
    static int param3;
    static int param4;
    
    int var1;
    int var2;
    
    int stat1;
    int stat2;
};


const map<string, int *> Component::__fields = Component::get_fields();
map<string, int *> Component::get_fields()
{
    map<string, int *> fields;
    fields["param1"] = &Component::param1;
    fields["param2"] = &Component::param2;
    return fields;
}

bool Component::__configured = false;
void Component::configure(map<string, int> &settings)
{
    __configured = true;
    map<string, int *>::const_iterator itr;
    for (itr = __fields.begin(); itr != __fields.end(); ++itr) {
        *(itr->second) = settings[itr->first];
    }
    param3 = param1+param2;
    param4 = param1*param2;
}

int Component::param1 = 0;
int Component::param2 = 0;
int Component::param3 = 0;
int Component::param4 = 0;

Component::Component(int arg1, int arg2)
{
    assert(Component::__configured);
    
    var1 = arg1;
    var2 = arg2;
    
    stat1 = 0;
    stat2 = 1;
}

Component::~Component()
{
}

int main()
{
    map<string, int> settings;
    settings["param1"] = 5;
    settings["param2"] = 6;
    Component::configure(settings);
    Component *component = new Component(3, 4);
    cout << component->param1 << endl;
    cout << component->param2 << endl;
    cout << component->param3 << endl;
    cout << component->param4 << endl;
    cout << component->var1 << endl;
    cout << component->var2 << endl;
    cout << component->stat1 << endl;
    cout << component->stat2 << endl;
    return 0;
}
