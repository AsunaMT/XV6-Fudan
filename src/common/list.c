#include <common/list.h>

void init_list_node(ListNode* node) {
    node->prev = node;
    node->next = node;
}

ListNode* _merge_list(ListNode* node1, ListNode* node2) {
    if (!node1)
        return node2;
    if (!node2)
        return node1;

    // before: (arrow is the next pointer)
    //   ... --> node1 --> node3 --> ...
    //   ... <-- node2 <-- node4 <-- ...
    //
    // after:
    //   ... --> node1 --+  +-> node3 --> ...
    //                   |  |
    //   ... <-- node2 <-+  +-- node4 <-- ...

    ListNode* node3 = node1->next;
    ListNode* node4 = node2->prev;

    node1->next = node2;
    node2->prev = node1;
    node4->next = node3;
    node3->prev = node4;

    return node1;
}

ListNode* _detach_from_list(ListNode* node) {
    ListNode* prev = node->prev;

    node->prev->next = node->next;
    node->next->prev = node->prev;
    init_list_node(node);

    if (prev == node)
        return NULL;
    return prev;
}

QueueNode* add_to_queue(QueueNode** head, QueueNode* node) {
    do
        node->next = *head;
    while (!__atomic_compare_exchange_n(head, &node->next, node, true, __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED));
    return node;
}

QueueNode* fetch_from_queue(QueueNode** head) {
    QueueNode* node;
    do
        node = *head;
    while (node
           && !__atomic_compare_exchange_n(head, &node, node->next, true, __ATOMIC_ACQ_REL,
                                           __ATOMIC_RELAXED));
    return node;
}

QueueNode* fetch_all_from_queue(QueueNode** head) {
    return __atomic_exchange_n(head, NULL, __ATOMIC_ACQ_REL);
}

void queue_init(Queue* x) {
    x->begin = x->end = 0;
    x->sz = 0;
    init_spinlock(&x->lk);
}
void queue_lock(Queue* x) {
    _acquire_spinlock(&x->lk);
}
void queue_unlock(Queue* x) {
    _release_spinlock(&x->lk);
}
void queue_push(Queue* x, ListNode* item) {
    init_list_node(item);
    if (x->sz == 0) {
        x->begin = x->end = item;

    } else {
        _merge_list(x->end, item);
        x->end = item;
    }
    x->sz++;
}
void queue_pop(Queue* x) {
    if (x->sz == 0)
        PANIC();
    if (x->sz == 1) {
        x->begin = x->end = 0;
    } else {
        auto t = x->begin;
        x->begin = x->begin->next;
        _detach_from_list(t);
    }
    x->sz--;
}
ListNode* queue_front(Queue* x) {
    if (!x || !x->begin)
        PANIC();
    return x->begin;
}
bool queue_empty(Queue* x) {
    return x->sz == 0;
}

void init_lfqnode(Node* node){
    node->next_ = NULL;
}

void init_lock_free_queue(LockFreeQueue* queue){
    queue->vhead.next_ = NULL;
    queue->head_ = &queue->vhead;
    queue->tail_ = &queue->vhead;
    queue->sz = 0;
}
void printk(const char *fmt, ...) ;
Node* dequeue(LockFreeQueue* queue) {
    //printk("de");
    Node *old_head, *first_node;
    do {
        old_head = queue->vhead.next_;
        first_node = old_head->next_;
        if (old_head == queue->tail_) {         
            if (old_head == NULL) {
                return NULL;
            }
            __sync_bool_compare_and_swap(&queue->tail_, old_head, &queue->vhead);
            continue;
        }
    } while(!__sync_bool_compare_and_swap(&queue->vhead.next_, old_head, first_node));
    //__sync_bool_compare_and_swap(&queue->tail_, dequeue_node, &queue->vhead);
    queue->sz--;
    return old_head;
}

void enqueue(LockFreeQueue* queue, Node *enqueue_node) {
    //printk("en");
    enqueue_node->next_ = NULL;
    Node *old_tail;
    while(true) {
        old_tail = queue->tail_;
        if (__sync_bool_compare_and_swap(&(old_tail->next_), NULL, enqueue_node)) {
            break;
        } else {
            /*全局尾指针不是指向最后一个节点，发生在其他线程已经完成节点添加操作，
             * 但是并没有更新最后一个节点，此时，当前线程的更新全局尾指针。
             * 为什么不直接continue 原因是: 如果多个线程同时添加了节点，但是都还没有更新尾节点，
             * 就会导致所有的线程循环，所以每一个线程必须主动以原子操作更新尾节点
             */
            __sync_bool_compare_and_swap(&queue->tail_, old_tail, old_tail->next_);
            continue;
      }
    } //while (!__sync_bool_compare_and_swap(&(old_tail->next_), NULL, enqueue_node));
    __sync_bool_compare_and_swap(&queue->tail_, old_tail, enqueue_node);//已经添加进去了，false也没关系。
    queue->sz++;
}


Node* lfqueue_front(LockFreeQueue* queue) {
    return queue->vhead.next_;
}

bool lfqueue_empty(LockFreeQueue* queue) {
    return queue->sz == 0;
}