#include <cassert>
#include <cstddef>
#include <iostream>

template<class DataType>
class Container
{
protected:
    size_t m_size; // there's always a upper limit for hardware container
    size_t m_length;
    DataType *m_data;

public:
    Container(int size) {
        assert(size > 0);
        this->m_size   = size;
        this->m_data   = new DataType[size];
        this->m_length = 0;
    }
    
    virtual ~Container() {
        delete [] this->m_data;
    }
    
    DataType &first() { assert(0); }
    DataType &last() { assert(0); }
    DataType &push() { assert(0); }
    DataType &pop() { assert(0); }
    DataType &shift() { assert(0); }
    DataType &unshift() { assert(0); }
    
    const bool is_full() {
        return this->m_length == this->m_size;
    }
    
    const bool is_empty() {
        return this->m_length == 0;
    }
    
    const int length() {
        return this->m_length;
    }
    
    const int size() {
        return this->m_size;
    }
    
    void clear() {
        this->m_length = 0;
    }
};

template<class DataType>
class Queue : public Container<DataType>
{
protected:
    size_t m_cursor;

public:
    Queue(int size) : Container<DataType>(size) {
        this->m_cursor = 0;
    }
    
    ~Queue() {
    }
    
    DataType &operator [](int index) {
        assert(index >= 0 && index < (int)this->m_length);
        
        return this->m_data[(this->m_cursor+index)%this->m_size];
    }
    
    DataType &first() {
        return (*this)[0];
    }
    
    DataType &last() {
        return (*this)[this->length-1];
    }
    
    DataType &push() {
        this->m_length += 1;
        DataType &data = (*this)[this->m_length-1];
        
        return data;
    }
    
    DataType &pop() {
        DataType &data = (*this)[this->m_length-1];
        this->m_length -= 1;
        
        return data;
    }
    
    DataType &unshift() {
        this->m_cursor = (this->m_cursor+this->m_size-1)%this->m_size;
        this->m_length += 1;
        DataType &data = (*this)[0];
        
        return data;
    }
    
    DataType &shift() {
        DataType &data = (*this)[0];
        this->m_cursor = (this->m_cursor+1)%this->m_size;
        this->m_length -= 1;
        
        return data;
    }
};

template<class DataType>
class LinkedList : public Container<DataType>
{
protected: 
    class Node {
    public:
        DataType *data;
        Node *next;
    };

    Node *m_nodes;
    Node *m_head;
    Node *m_tail;
    Node *m_free;
    bool m_log;

public:
    LinkedList(int size, bool log = false) : Container<DataType>(size) {
        this->m_nodes = new Node[size];
        for (int i=0; i<size; ++i) {
            this->m_nodes[i].data = &(this->m_data[i]);
            this->m_nodes[i].next = &(this->m_nodes[i+1]);
        }
        this->m_nodes[size-1].next = NULL;
        
        this->m_head = NULL;
        this->m_tail = NULL;
        this->m_free = this->m_nodes;
        
        this->m_log = log;
    }
    
    virtual ~LinkedList() {
        delete [] this->m_nodes;
    }
    
    DataType &first() {
        assert(this->m_length > 0);
        
        return *(this->m_head->data);
    }
    
    DataType &last() {
        assert(this->m_length > 0);
        
        return *(this->m_tail->data);
    }
    
    DataType &push() {
        assert(this->m_length < this->m_size);
        
        Node *node = this->m_free;
        this->m_free = this->m_free->next;
        node->next = NULL;
        
        if (this->m_head == NULL) {
            this->m_head = node;
            this->m_tail = node;
        } else {
            this->m_tail->next = node;
            this->m_tail = node;
        }
        this->m_length += 1;
        
        return *(node->data);
    }
    
    DataType &unshift() {
        assert(this->m_length < this->m_size);
        
        Node *node = this->m_free;
        this->m_free = this->m_free->next;
        node->next = NULL;
        
        if (this->m_head == NULL) {
            this->m_head = node;
            this->m_tail = node;
        } else {
            node->next = this->m_head;
            this->m_head = node;
        }
        this->m_length += 1;
        
        return *(node->data);
    }
    
    DataType &shift() {
        assert(this->m_length > 0);
        
        Node *node = this->m_head;
        this->m_head = this->m_head->next;
        if (this->m_head == NULL) {
            this->m_tail == NULL;
        }
        this->m_length -= 1;
        
        node->next = this->m_free;
        this->m_free = node;
        
        return *(node->data);
    }
    
    class Iterator {
    protected:
        Node *prev;
        Node *node;
        
    public:
        DataType &operator *() {
            return *(node->data);
        }
        friend class LinkedList;
    };
    
    void reset(Iterator &iter) {
        iter.prev = NULL;
        iter.node = NULL;
    }
    
    bool next(Iterator &iter) {
        iter.prev = iter.node;
        if (iter.node == NULL) {
            iter.node = this->m_head;
        } else {
            iter.node = iter.node->next;
        }
        
        return iter.node;
    }
    
    void remove(Iterator &iter) {        
        if (iter.prev == NULL) {
            this->m_head = iter.node->next;
        } else {
            iter.prev->next = iter.node->next;
        }
        if (iter.node == this->m_tail) {
            this->m_tail = iter.prev;
        }
        this->m_length -= 1;
        
        iter.node->next = this->m_free;
        this->m_free = iter.node;
        
        iter.node = iter.prev;
    }
};
