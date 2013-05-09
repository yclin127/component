#include <cassert>

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
    
    DataType &enque() { assert(0); }
    DataType &deque() { assert(0); }
    
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
class Stack : public Container<DataType>
{
public:
    Stack(int size) : Container<DataType>(size) {
    }
    
    ~Stack() {
    }
    
    DataType &operator [](int index) {
        assert(index >=0 && index < this->length);
        return this->data[this->length-index-1];
    }
    
    DataType &enque() {
        assert(this->m_length < this->size);
        DataType &data = this->m_data[this->m_length];
        this->m_length += 1;
        return data;
    }
    
    DataType &deque() {
        assert(this->m_length > 0);
        DataType &data = this->m_data[this->m_length-1];
        this->m_length -= 1;
        return data;
    }
};

template<class DataType>
class RingBuffer : public Container<DataType>
{
protected:
    size_t m_cursor;

public:
    RingBuffer(int size) : Container<DataType>(size) {
        this->m_cursor = 0;
    }
    
    ~RingBuffer() {
    }
    
    DataType &operator [](int index) {
        assert(index >=0 && index < this->length);
        return this->m_data[(this->m_cursor+index)%this->m_size];
    }
    
    DataType &enque() {
        assert(this->m_length < this->size);
        DataType &data = this->m_data[(this->m_cursor+this->m_length)%this->m_size];
        this->m_length += 1;
        return data;
    }
    
    DataType &deque() {
        assert(this->m_length > 0);
        DataType &data = this->m_data[this->m_cursor];
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
    Node *m_last;
    
    void drop(Node *ahead, Node *back) {
        // remove (ahead, back]
        Node *front;
        if (ahead == NULL) {
            front = this->m_head;
            this->m_head = back->next;
        } else {
            front = ahead->next;
            ahead->next = back->next;
        }
        // append (ahead, back] to the end
        if (this->m_tail == NULL)
            this->m_tail = front;
        this->m_last->next = front;
        this->m_last = back;
        back->next = NULL;
    }

public:
    LinkedList(int size) : Container<DataType>(size) {
        this->m_nodes = new Node[size];
        for (int i=0; i<size; ++i) {
            this->m_nodes[i].data = &(this->m_data[i]);
            this->m_nodes[i].next = &(this->m_nodes[i+1]);
        }
        this->m_last = &(this->m_nodes[size-1]);
        this->m_last->next = NULL;
        
        this->m_head = this->m_nodes;
        this->m_tail = this->m_nodes;
    }
    
    virtual ~LinkedList() {
        delete [] this->m_nodes;
    }
    
    DataType &enque() {
        assert(this->m_length < this->m_size);
        DataType &data = *(this->m_tail->data);
        this->m_tail = this->m_tail->next;
        this->m_length += 1;
        return data;
    }
    
    DataType &deque() {
        assert(this->m_length > 0);
        DataType &data = *(this->m_head->data);
        this->drop(NULL, this->m_head);
        this->m_length -= 1;
        return data;
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
        return iter.node != this->m_tail;
    }
    bool remove(Iterator &iter) {
        this->drop(iter.prev, iter.node);
        iter.node = iter.prev;
    }
};
