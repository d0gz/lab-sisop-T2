#include <linux/list.h>


static int queue_capacity = 10;
static int max_msg_bytes = 128;
MODULE_PARM_DESC(capacity, "Maximum messages per subscription queue (default 10).");
MODULE_PARM_DESC(msg_bytes, "Maximum message bytes (default 128).");
module_param(queue_capacity, int, 0);
module_param(max_msg_bytes, int, 0);

#define MSG_SIZE 128
#define NAME_SIZE 32

struct message_s {
    struct list_head link;
    char message[MSG_SIZE];
    short size;
};

struct process_subscription {
    struct list_head process_link;
    struct list_head message_list_head; 

    int current_count;
    int capacity;

    char name[NAME_SIZE];
    int pid;
};

struct topic {
    struct list_head topic_link;
    struct list_head subscription_list_head;
    
    char topic_name[NAME_SIZE];
};

extern struct list_head global_topic_list_head;