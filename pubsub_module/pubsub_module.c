#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/ctype.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andre, João, Ricardo e Henrique");
MODULE_DESCRIPTION("Trabalho 2 - Laboratorio de SISOP");

static int queue_capacity = -1;
static int max_msg_bytes = -1;
module_param(queue_capacity, int, 0444);
module_param(max_msg_bytes, int, 0444);
MODULE_PARM_DESC(queue_capacity, "Maximum messages per subscription queue (default 10)");
MODULE_PARM_DESC(max_msg_bytes, "Maximum bytes per message (default 128)");

#define NAME_SIZE 32
#define MSG_STRUCT_SIZE 256

struct message_s {
    struct list_head link;
    size_t size;
    char *data;
};

struct process_subscription {
    struct list_head process_link;
    struct list_head message_list_head;
    int current_count;
    int capacity;
    int pid;
    char name[NAME_SIZE];
};

struct topic {
    struct list_head topic_link;
    struct list_head subscription_list_head;
    char topic_name[NAME_SIZE];
    struct list_head topic_node;
};

static LIST_HEAD(global_topic_list_head);

static DEFINE_MUTEX(pubsub_lock);

static struct topic *find_topic(const char *tname);
static struct process_subscription *find_subscription(struct topic *t, int pid);
static struct process_subscription *create_subscription(struct topic *t, int pid, const char *pname);
static struct topic *create_topic(const char *tname);

static struct topic *find_topic(const char *tname)
{
    struct topic *t;
    list_for_each_entry(t, &global_topic_list_head, topic_link) {
        if (strncmp(t->topic_name, tname, NAME_SIZE) == 0)
            return t;
    }
    return NULL;
}

static struct process_subscription *find_subscription(struct topic *t, int pid)
{
    struct process_subscription *p;
    list_for_each_entry(p, &t->subscription_list_head, process_link) {
        if (p->pid == pid)
            return p;
    }
    return NULL;
}

static struct topic *create_topic(const char *tname)
{
    struct topic *t = kmalloc(sizeof(*t), GFP_KERNEL);
    if (!t) return NULL;
    strncpy(t->topic_name, tname, NAME_SIZE - 1);
    t->topic_name[NAME_SIZE - 1] = '\0';
    INIT_LIST_HEAD(&t->subscription_list_head);
    INIT_LIST_HEAD(&t->topic_link);
    list_add_tail(&t->topic_link, &global_topic_list_head);
    printk(KERN_INFO "pubsub: created topic '%s'\n", t->topic_name);
    return t;
}

static struct process_subscription *create_subscription(struct topic *t, int pid, const char *pname)
{
    struct process_subscription *p = kmalloc(sizeof(*p), GFP_KERNEL);
    if (!p) return NULL;
    INIT_LIST_HEAD(&p->message_list_head);
    p->current_count = 0;
    p->capacity = queue_capacity;
    p->pid = pid;
    strncpy(p->name, pname ? pname : "user", NAME_SIZE - 1);
    p->name[NAME_SIZE - 1] = '\0';
    INIT_LIST_HEAD(&p->process_link);
    list_add_tail(&p->process_link, &t->subscription_list_head);
    printk(KERN_INFO "pubsub: pid %d subscribed to topic '%s' (capacity=%d)\n", pid, t->topic_name, p->capacity);
    return p;
}

static void free_subscription(struct topic *t, struct process_subscription *p)
{
    struct message_s *m, *tmp;
    list_for_each_entry_safe(m, tmp, &p->message_list_head, link) {
        list_del(&m->link);
        kfree(m->data);
        kfree(m);
    }
    list_del(&p->process_link);
    kfree(p);
}

static void free_topic(struct topic *t)
{
    struct process_subscription *p, *ptmp;
    list_for_each_entry_safe(p, ptmp, &t->subscription_list_head, process_link) {
        free_subscription(t, p);
    }
    list_del(&t->topic_link);
    kfree(t);
}

int topic_subscribe_process(int pid, const char *pname, const char *tname)
{
    struct topic *t;
    struct process_subscription *sub;

    if (!tname) return -EINVAL;
    mutex_lock(&pubsub_lock);
    t = find_topic(tname);
    if (!t) {
        t = create_topic(tname);
        if (!t) {
            mutex_unlock(&pubsub_lock);
            return -ENOMEM;
        }
    }

    if (find_subscription(t, pid)) {
        mutex_unlock(&pubsub_lock);
        return -EALREADY;
    }

    sub = create_subscription(t, pid, pname);
    if (!sub) {
        mutex_unlock(&pubsub_lock);
        return -ENOMEM;
    }

    mutex_unlock(&pubsub_lock);
    return 0;
}

int topic_unsubscribe_process(int pid, const char *tname)
{
    struct topic *t;
    struct process_subscription *sub;
    if (!tname) return -EINVAL;

    mutex_lock(&pubsub_lock);
    t = find_topic(tname);
    if (!t) {
        mutex_unlock(&pubsub_lock);
        return -ENOENT;
    }

    sub = find_subscription(t, pid);
    if (!sub) {
        mutex_unlock(&pubsub_lock);
        return -ENOENT;
    }

    if (!list_empty(&sub->message_list_head)) 
    {
        printk(KERN_WARNING "Process %d unsubscribing from %s, but message queue NOT EMPTY. All messages will be deleted.\n", pid, tname);
        mutex_unlock(&pubsub_lock);
        return 0;
    } 

    free_subscription(t, sub);

    if (list_empty(&t->subscription_list_head)) {
        free_topic(t);
    }

    mutex_unlock(&pubsub_lock);
    return 0;
}

int topic_publish_message(const char *tname, const char *data)
{
    struct topic *t;
    struct process_subscription *p;
    struct message_s *new_msg;
    size_t msg_len;
    char *payload;

    if (!tname || !data) return -EINVAL;

    msg_len = strnlen(data, max_msg_bytes+1);
    if (msg_len == 0) return 0;
    if (msg_len > max_msg_bytes) {
        printk(KERN_WARNING "Message length exceeds max_msg_bytes %d.\n", max_msg_bytes);
        return 0; 
    }
    

    mutex_lock(&pubsub_lock);
    t = find_topic(tname);
    if (!t) {
        mutex_unlock(&pubsub_lock);
        return -ENOENT;
    }

    if (list_empty(&t->subscription_list_head)) {
        printk(KERN_INFO "pubsub: publish to '%s' but no subscribers\n", tname);
        mutex_unlock(&pubsub_lock);
        return 0;
    }

    list_for_each_entry(p, &t->subscription_list_head, process_link) {
        if (p->current_count >= p->capacity) {
            struct message_s *oldest = list_first_entry(&p->message_list_head, struct message_s, link);
            list_del(&oldest->link);
            kfree(oldest->data);
            kfree(oldest);
            p->current_count--;
            printk(KERN_INFO "pubsub: pid %d queue full, dropped oldest message\n", p->pid);
        }

        new_msg = kmalloc(sizeof(*new_msg), GFP_KERNEL);
        if (!new_msg) {
            printk(KERN_ERR "pubsub: kmalloc message struct failed\n");
            continue;
        }

        payload = kmalloc(msg_len + 1, GFP_KERNEL);
        if (!payload) {
            kfree(new_msg);
            printk(KERN_ERR "pubsub: kmalloc payload failed\n");
            continue;
        }

        memcpy(payload, data, msg_len);
        payload[msg_len] = '\0';

        INIT_LIST_HEAD(&new_msg->link);
        new_msg->size = msg_len;
        new_msg->data = payload;

        list_add_tail(&new_msg->link, &p->message_list_head);
        p->current_count++;
    }

    printk(KERN_INFO "pubsub: published message to topic '%s'\n", tname);
    mutex_unlock(&pubsub_lock);
    return 0;
}

int topic_fetch_message_into_buffer(int pid, const char *tname, char **out_buf, size_t *out_len)
{
    struct topic *t;
    struct process_subscription *sub;
    struct message_s *m;

    if (!tname || !out_buf || !out_len) return -EINVAL;

    mutex_lock(&pubsub_lock);
    t = find_topic(tname);
    if (!t) {
        mutex_unlock(&pubsub_lock);
        return -ENOENT;
    }

    sub = find_subscription(t, pid);
    if (!sub) {
        mutex_unlock(&pubsub_lock);
        return -EPERM;
    }

    if (list_empty(&sub->message_list_head)) {
        mutex_unlock(&pubsub_lock);
        return -ENODATA;
    }

    m = list_first_entry(&sub->message_list_head, struct message_s, link);

    *out_buf = kmalloc(m->size + 1, GFP_KERNEL);
    if (!*out_buf) {
        mutex_unlock(&pubsub_lock);
        return -ENOMEM;
    }
    memcpy(*out_buf, m->data, m->size);
    (*out_buf)[m->size] = '\0';
    *out_len = m->size;

    list_del(&m->link);
    kfree(m->data);
    kfree(m);
    sub->current_count--;

    printk(KERN_INFO "pubsub: pid %d fetched a message from topic '%s'\n", pid, tname);
    mutex_unlock(&pubsub_lock);
    return 0;
}

void topic_cleanup_all(void)
{
    struct topic *t, *t_tmp;
    list_for_each_entry_safe(t, t_tmp, &global_topic_list_head, topic_link) {
        free_topic(t);
    }
}

struct pubsub_filedata {
    char *fetch_buf;
    size_t fetch_len;
};

static int handle_command(struct file *file, const char *buf, size_t count)
{
    char *kbuf;
    char cmd[32];
    char topic[NAME_SIZE];
    char *rest;
    int ret = 0;
    int pid = task_pid_nr(current);

    if (count == 0) return -EINVAL;
    if (count > 1024) return -E2BIG;

    kbuf = kmalloc(count + 1, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;
    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }
    kbuf[count] = '\0';

    if (sscanf(kbuf, "%31s %31s", cmd, topic) < 1) {
        kfree(kbuf);
        return -EINVAL;
    }

    rest = NULL;
    if (strcmp(cmd, "/publish") == 0) {
        char *p = kbuf;
        int i = 0;
        while (i < count && *p && !isspace(*p)) { p++; i++; }
        while (i < count && *p && isspace(*p)) { p++; i++; }
        while (i < count && *p && !isspace(*p)) { p++; i++; }
        if (i < count && *p) {
            while (i < count && *p && isspace(*p)) { p++; i++; }
            if (i <= count)
                rest = p;
        } else {
            rest = NULL;
        }
    }

    if (strcmp(cmd, "/subscribe") == 0) {
        ret = topic_subscribe_process(pid, "user", topic);
        if (ret == -EALREADY) ret = 0;
    } else if (strcmp(cmd, "/unsubscribe") == 0) {
        ret = topic_unsubscribe_process(pid, topic);
    } else if (strcmp(cmd, "/publish") == 0) {
        if (!rest) {
            ret = -EINVAL;
        } else {
            ret = topic_publish_message(topic, rest);
        }
    } else if (strcmp(cmd, "/fetch") == 0) {
        struct pubsub_filedata *fddata = file->private_data;
        char *out = NULL;
        size_t out_len = 0;
        ret = topic_fetch_message_into_buffer(pid, topic, &out, &out_len);
        if (ret == 0) {
            if (fddata->fetch_buf) {
                kfree(fddata->fetch_buf);
                fddata->fetch_buf = NULL;
                fddata->fetch_len = 0;
            }
            fddata->fetch_buf = out;
            fddata->fetch_len = out_len;
        }
    } else {
        ret = -EINVAL;
    }

    kfree(kbuf);
    return ret;
}

static int pubsub_open(struct inode *inode, struct file *file)
{
    struct pubsub_filedata *fd = kmalloc(sizeof(*fd), GFP_KERNEL);
    if (!fd) return -ENOMEM;
    fd->fetch_buf = NULL;
    fd->fetch_len = 0;
    file->private_data = fd;
    return 0;
}

static int pubsub_release(struct inode *inode, struct file *file)
{
    struct pubsub_filedata *fd = file->private_data;
    if (fd) {
        if (fd->fetch_buf) kfree(fd->fetch_buf);
        kfree(fd);
    }
    file->private_data = NULL;
    return 0;
}

static ssize_t pubsub_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret = handle_command(file, buf, count);
    if (ret < 0) return ret;
    return count;
}

static ssize_t pubsub_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct pubsub_filedata *fd = file->private_data;
    size_t to_copy;

    if (!fd) return -EFAULT;
    if (!fd->fetch_buf) return 0;

    to_copy = min(fd->fetch_len, count);
    if (copy_to_user(buf, fd->fetch_buf, to_copy))
        return -EFAULT;

    kfree(fd->fetch_buf);
    fd->fetch_buf = NULL;
    fd->fetch_len = 0;

    return to_copy;
}

static const struct file_operations pubsub_fops = {
    .owner = THIS_MODULE,
    .open = pubsub_open,
    .release = pubsub_release,
    .write = pubsub_write,
    .read = pubsub_read,
};

static struct miscdevice pubsub_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "pubsub",
    .fops = &pubsub_fops,
    .mode = 0666,
};

static int __init pubsub_init_module(void)
{
    if(queue_capacity <= 0 || max_msg_bytes <= 0){
        printk(KERN_ALERT "Parameters not set, to set use example: modprobe pubsub queue_capacity=\"5\" max_msg_bytes=10\n");
    }
    if(queue_capacity <= 0){
        printk(KERN_ALERT "queue_capacity not loaded. Setting queue_capacity to default size 5!\n");
        queue_capacity = 5;
    }
    if(max_msg_bytes <= 0){
        printk(KERN_ALERT "max_msg_bytes not loaded. Setting max_msg_bytes to default size 10!\n");
        max_msg_bytes = 10;
    }

    int rc = misc_register(&pubsub_dev);
    if (rc) {
        printk(KERN_ERR "pubsub: misc_register failed\n");
        return rc;
    }
    printk(KERN_INFO "pubsub: module loaded, device /dev/%s\n", pubsub_dev.name);
    return 0;
}

static void __exit pubsub_cleanup_module(void)
{
    misc_deregister(&pubsub_dev);
    mutex_lock(&pubsub_lock);
    topic_cleanup_all();
    mutex_unlock(&pubsub_lock);
    printk(KERN_INFO "pubsub: module unloaded and cleaned up\n");
}

module_init(pubsub_init_module);
module_exit(pubsub_cleanup_module);
