
#include <iostream>
#include "container.h"

int main()
{
    int size = 16;
    LinkedList<int> container(size);
    LinkedList<int>::Iterator iter;
    
    for (int i=2; i<size; i++) {
        container.enque() = i;
    }
    
    for (int i=2; i*i<=size; i++) {
        container.reset(iter);
        while (container.next(iter)) {
            if ((*iter) > i && (*iter) % i == 0)
                container.remove(iter);
        }
    }
    
    return 0;
}
