#define FUSE_USE_VERSION 31

#define _GNU_SOURCE
#include <fuse3/fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <assert.h>

// #define DEBUG

#ifdef DEBUG
    #define dbg_printf(...) printf(__VA_ARGS__);
#else
    #define dbg_printf(...)
#endif

#define offset_of(type, member) ((size_t)( &(((type*)0)->member) ))

#define container_of(ptr, type, member) ({ \
        const typeof( ((type *)0)->member ) *__mptr = (ptr); \
        (type *)( (char *)__mptr - offset_of(type, member) ); \
    })

struct list_node {
    struct list_node *next;
    struct list_node *prev;
} list_head;

static void list_init(struct list_node *list) {
    list->next = list;
    list->prev = list;
}
  
static void list_add(struct list_node *new, struct list_node *prev, struct list_node *next) {
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}
  
static inline void list_add_head(struct list_node *new, struct list_node *head) {
    list_add(new, head, head->next);
}
  
static inline void list_add_tail(struct list_node *new, struct list_node *head) {
    list_add(new, head->prev, head);
}
  
static inline void list_del(struct list_node *entry) {
    struct list_node *prev = entry->prev;
    struct list_node *next = entry->next;
    next->prev = prev;
    prev->next = next;
}

static struct cli_options {
    int show_help;
    int chatbot_mode;
    char *bot1;
    char *bot2;
} options;

#define OPTION(t, p) \
    { t, offset_of(struct cli_options, p), 1 }

static const struct fuse_opt option_spec[] = {
    OPTION("-b", chatbot_mode), OPTION("--bot", chatbot_mode),
    OPTION("-h", show_help), OPTION("--help", show_help),
    OPTION("--bot1=%s", bot1), OPTION("--bot2=%s", bot2),
    FUSE_OPT_END
};

#define MAX_FILE_NAME_LEN 1024
#define BLOCK_SIZE 512
#define MAX_EMOJI_NUM 100

#define IS_ROOT_PATH(path) (strcmp(path, "/") == 0) 
#define MIN(a, b) ((a) < (b)? (a): (b))
#define MAX(a, b) ((a) > (b)? (a): (b))

typedef struct list_node entry_iter;
#define initializer(attr) list_init(&list_head)

struct file_entry {
    char *path;
    char *content;
    struct stat status;
    entry_iter node;
};

#define SIZE(entry) ((entry)->status.st_size)
#define MODE(entry) ((entry)->status.st_mode)
#define BLOK(entry) ((entry)->status.st_blocks)
#define ATIM(entry) ((entry)->status.st_atime)
#define CTIM(entry) ((entry)->status.st_ctime)
#define MTIM(entry) ((entry)->status.st_mtime)
#define IS_DIR(entry) ((MODE(entry) & ~0111) == __S_IFDIR)
#define IS_REG(entry) ((MODE(entry) & ~0111) == __S_IFREG)

#define get_entry(ptr) container_of(ptr, struct file_entry, node)
#define insert_entry(entry) list_add_tail(&entry->node, &list_head)
#define remove_entry(entry) list_del(&entry->node) 
#define foreach_entry(ptr) \
    for((ptr) = list_head.next; (ptr) != &list_head; (ptr) = (ptr)->next)

static struct file_entry* find_entry(const char *path) {
    entry_iter *ptr;
    foreach_entry(ptr) {
        struct file_entry *entry = get_entry(ptr);
        if (!strcmp(entry->path, path)) return entry;
    }
    return NULL;
}

#ifdef DEBUG
    #define print_entries(attr) do { \
            printf("[entries]\n"); \
            entry_iter *ptr; \
            foreach_entry(ptr) { \
                struct file_entry *entry = get_entry(ptr); \
                printf("%s dir:%d reg:%d\n", entry->path, IS_DIR(entry), IS_REG(entry)); \
            } \
            printf("\n"); \
        } while(0)
#else   
    #define print_entries(attr) 
#endif

int chatbot_mode = 0;
char *bot1_path, *bot2_path;
int emoji_count = 0;

static int is_parent_path(const char *path1, const char* path2, int strict) {
    int len1 = strnlen(path1, MAX_FILE_NAME_LEN);
    int len2 = strnlen(path2, MAX_FILE_NAME_LEN);
    if (path1[len1 - 1] == '/') --len1;
    if (path2[len2 - 1] == '/') --len2;
    return len1 < len2 && !strncmp(path1, path2, len1) 
        && path2[len1] == '/' && (!strict || !strchr(path2 + len1 + 1, '/'));
}

static char* get_relative_name(const char* path1, const char* path2) {
    int len1 = strnlen(path1, MAX_FILE_NAME_LEN);
    int len2 = strnlen(path2, MAX_FILE_NAME_LEN);
    if (path1[len1 - 1] == '/') --len1;
    if (path2[len2 - 1] == '/') --len2;
    const char *first = path2 + len1 + 1;
    const char *last = strchr(first, '/');
    if (!last) last = path2 + len2;
    return strndup(first, last - first);
}

static char* concatenate(const char* str1, const char* str2) {
    char *str = calloc(MAX_FILE_NAME_LEN, sizeof(char));
    strncpy(str, str1, MAX_FILE_NAME_LEN);
    strncat(str, str2, MAX_FILE_NAME_LEN - strnlen(str1, MAX_FILE_NAME_LEN));
    return str;
}

static char *replace_first_after(const char* parent, const char *path, const char *old_str, const char *new_str) {
    int plen = strnlen(parent, MAX_FILE_NAME_LEN);
    char* str = calloc(MAX_FILE_NAME_LEN, sizeof(char));
    strncpy(str, path, MAX_FILE_NAME_LEN);
    char *pos = strstr(str + plen, old_str); 
    
    if (pos != NULL) {
        size_t old_len = strnlen(old_str, MAX_FILE_NAME_LEN); 
        size_t new_len = strnlen(new_str, MAX_FILE_NAME_LEN); 
        memmove(pos + new_len, pos + old_len, strlen(pos + old_len) + 1); 
        memcpy(pos, new_str, new_len); 
    }

    return str;
}

static char* swtich_bot(const char* path) {
    
    if (is_parent_path(bot1_path, path, 0)) {
        return replace_first_after("/", path, options.bot1, options.bot2);
    }
    if (is_parent_path(bot2_path, path, 0)) {
        return replace_first_after("/", path, options.bot2, options.bot1);
    }
    return NULL;
}

#define EMOJI_PREFIX ":emoji"
char* emoji[MAX_EMOJI_NUM];

static int get_emoji_id(const char* buf) {
    if (strncmp(buf, EMOJI_PREFIX, strlen(EMOJI_PREFIX))) return -1;
    return atoi(buf + strlen(EMOJI_PREFIX));
}

static struct file_entry* new_file(char* path, char* content, mode_t mode, dev_t rdev, nlink_t nlink) {
    struct file_entry *entry = malloc(sizeof(struct file_entry));
    int size = content? strnlen(content, MAX_FILE_NAME_LEN): 0;
    *entry = (struct file_entry) {
        .path = path,
        .content = content,
        .status = (struct stat) {
            .st_mode = mode,
            .st_rdev = rdev,
            .st_nlink = nlink, 
            .st_size = size,
            .st_blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE,
            .st_atime = time(NULL),
            .st_ctime = time(NULL),
            .st_mtime = time(NULL),
        }
    };
    dbg_printf("create file %s dir:%d reg:%d\n", path, IS_DIR(entry), IS_REG(entry));
    list_init(&entry->node);
    insert_entry(entry);
    return entry;
}

static void del_file(struct file_entry *entry) {
    remove_entry(entry);
    if (entry->path) free(entry->path);
    if (entry->content) free(entry->content);
    free(entry);
}

static void* keibot_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    dbg_printf("[init]\n");
    cfg->kernel_cache = 1;
    initializer(NULL);
    new_file(strdup("/"), NULL, __S_IFDIR | 0755, 0, 2);
    if (options.chatbot_mode) {
        chatbot_mode = 1;
        bot1_path = concatenate("/", options.bot1);
        bot2_path = concatenate("/", options.bot2);
        new_file(bot1_path, NULL, __S_IFDIR | 0755, 0, 2);
        new_file(bot2_path, NULL, __S_IFDIR | 0755, 0, 2);
        emoji[emoji_count++] = strdup("( ﾟ▽ﾟ)/\n");
        emoji[emoji_count++] = strdup("(°∀°)b\n");
        emoji[emoji_count++] = strdup("(¬‿¬)\n");
        emoji[emoji_count++] = strdup("(▼-▼*)\n");
        emoji[emoji_count++] = strdup("(´Д｀)\n");
    }
    return NULL;
}

static void keibot_destroy(void *private_data) {
    dbg_printf("[destroy]\n");
    entry_iter *ptr, *nex;
    for(ptr = list_head.next; ptr != &list_head; ptr = nex) {
        struct file_entry *entry = get_entry(ptr);
        nex = ptr->next;
        del_file(entry);
    }
    if (options.chatbot_mode) {
        free(bot1_path);
        free(bot2_path);
        while(emoji_count) free(emoji[--emoji_count]);
    }
}

static int keibot_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    dbg_printf("[getattr] get attribute of %s\n", path);

    memset(stbuf, 0, sizeof(struct stat));
    dbg_printf("attr of common file\n");
    struct file_entry *entry = find_entry(path);
    dbg_printf("end searching\n");
    if (!entry) return -ENOENT;
    *stbuf = entry->status;
    dbg_printf("end getattr\n");
    return 0;
}

static int keibot_readdir(const char* path, void *buf, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    dbg_printf("[readdir] read directory contents %s\n", path);

    print_entries(NULL);

    filler(buf, ".", NULL, 0, 0);
    if (!IS_ROOT_PATH(path)) filler(buf, "..", NULL, 0, 0);
    if (!find_entry(path)) return -ENOENT;

    entry_iter *ptr;
    foreach_entry(ptr) {
        struct file_entry *entry = get_entry(ptr);
        if (is_parent_path(path, entry->path, 1)) {
            char* name = get_relative_name(path, entry->path);
            filler(buf, name, &entry->status, 0, 0);
            free(name);
        }
    }

    return 0;
}

static int create_file(const char* path, mode_t mode, dev_t rdev, nlink_t nlink) {
    if (strlen(path) > MAX_FILE_NAME_LEN) return -ENAMETOOLONG;
    if (find_entry(path) != NULL) return -EEXIST;
    new_file(strdup(path), strdup(""), mode, rdev, nlink);
    return 0;
}

static int keibot_mknod(const char *path, mode_t mode, dev_t rdev) {
    dbg_printf("[mknod] create node %s %o %lu\n", path, mode, rdev);
    
    int ret = create_file(path, __S_IFREG | mode, rdev, 1);
    if (ret < 0) return ret;
    
    char *npath;
    if (chatbot_mode && (npath = swtich_bot(path))) {
        ret = create_file(npath, __S_IFREG | mode, rdev, 1);
        free(npath);
    }
    return ret;
}

static int keibot_mkdir(const char *path, mode_t mode) {
    dbg_printf("[mkdir] create dir %s %o\n", path, mode);

    int ret = create_file(path, __S_IFDIR | mode, 0, 2);
    if (ret < 0) return ret;
    
    char *npath;
    if (chatbot_mode && (npath = swtich_bot(path))) {
        ret = create_file(npath, __S_IFDIR | mode, 0, 2);
        free(npath);
    }
    return ret;
}

static int keibot_unlink(const char *path) {
    dbg_printf("[unlink] remove node %s\n", path);
    
    struct file_entry *entry = find_entry(path);
    if (!entry) return -ENOENT;
    if (IS_DIR(entry)) return -EISDIR;
    
    del_file(entry);
    return 0;
}

static int keibot_rmdir(const char *path) {
    dbg_printf("[unlink] remove node %s\n", path);
    
    struct file_entry *entry = find_entry(path);
    if (!entry) return -ENOENT;
    if (IS_REG(entry)) return -EISNAM;

    entry_iter *ptr;
    int subcount = 0;
    foreach_entry(ptr) {
        struct file_entry *entry = get_entry(ptr);
        if (is_parent_path(path, entry->path, 0)) subcount++;
    }
    if (subcount > 0) return -ENOTEMPTY;
    
    del_file(entry);
    return 0;
}

static int write_file(struct file_entry* entry, const char* buf, size_t bytes, off_t offset) {
    if (IS_DIR(entry)) return -EISDIR;

    if (offset + bytes > SIZE(entry)) {
        void *content = realloc(entry->content, offset + bytes);
        if (!content) return -ENOMEM;
        entry->content = content;
    }
    memcpy(entry->content + offset, buf, bytes);
    entry->content[offset + bytes] = '\0';
    SIZE(entry) = offset + bytes;
    BLOK(entry) = (offset + bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    return 0;
}

static int keibot_write(const char *path, const char *buf, size_t bytes, off_t offset, struct fuse_file_info* fi) {
    dbg_printf("[write] write into %s (off %ld), from %s (%luB)\n", path, offset, buf, bytes);

    // struct file_entry *entry = find_entry(path);
    // if (!entry) return -ENOENT;
    if (!fi || !fi->fh) return -ENOENT;
    struct file_entry *entry = (struct file_entry*)fi->fh;
    int ret, id;
    if (chatbot_mode && (id = get_emoji_id(buf)) >= 0) {
        ret = write_file(entry, emoji[id], strlen(emoji[id]), 0);
    } else {
        ret = write_file(entry, buf, bytes, offset);
    }
    if (ret < 0) return ret;

    char* npath;
    if (chatbot_mode && (npath = swtich_bot(path))) {
        if((entry = find_entry(npath))) {
            if ((id = get_emoji_id(buf)) >= 0) {
                ret = write_file(entry, emoji[id], strlen(emoji[id]), 0);
            } else {
                ret = write_file(entry, buf, bytes, offset);
            }
        }
        else ret = -ENOENT;
        free(npath);
    }
    
    return bytes;
}

static int keibot_read(const char *path, char *buf, size_t bytes, off_t offset, struct fuse_file_info *fi) {
    dbg_printf("[read] read from %s (off %ld, %luB)\n", path, offset, bytes);
    
    // struct file_entry *entry = find_entry(path);
    // if (!entry) return -ENOENT;
    if (!fi || !fi->fh) return -ENOENT; 
    struct file_entry *entry = (struct file_entry*) fi->fh;
    if (IS_DIR(entry)) return -EISDIR;
    
    size_t rbytes = MIN(MAX(SIZE(entry) - offset, 0), bytes);
    memcpy(buf, entry->content + offset, rbytes);
    return rbytes;
}

static int keibot_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    dbg_printf("[utimes] update times %s\n", path);
    
    struct file_entry *entry = find_entry(path);
    if (!entry) return -ENOENT;
    
    ATIM(entry) = time(NULL);
    CTIM(entry) = time(NULL);
    MTIM(entry) = time(NULL);
    return 0;
}


static int keibot_open(const char* path, struct fuse_file_info *fi) {
    dbg_printf("[open!] open file %s\n", path);
    struct file_entry *entry = find_entry(path);
    if (!entry) entry = new_file(strdup(path), strdup(""), __S_IFREG | 0444, 0, 1);
    if (IS_DIR(entry)) return -EISDIR;
    fi->fh = (uint64_t) entry;
    return 0;
}

static int keibot_opendir(const char *path, struct fuse_file_info *fi) {
    dbg_printf("[opendir!] open directory %s\n", path);
    struct file_entry *entry = find_entry(path);
    if (!entry) entry = new_file(strdup(path), NULL, __S_IFDIR | 0755, 0, 2);
    if (IS_REG(entry)) return -EISNAM;
    fi->fh = (uint64_t) entry;
    return 0;
}

static int keibot_release(const char *path, struct fuse_file_info *fi) {
    dbg_printf("[release!] %s\n", path);
    fi->fh = 0;
    return 0;
}

int keibot_releasedir(const char *path, struct fuse_file_info *fi) {
    dbg_printf("[releasedir!] %s\n", path);
    fi->fh = 0;
    return 0;
}

static int keibot_access(const char* path, int mask) {
    dbg_printf("[access!] %s\n", path);
    return 0;
}

static int keibot_rename(const char *src_path, const char *dest_path, unsigned int flags) {
    dbg_printf("[rename] from %s to %s, with flag %u\n", src_path, dest_path, flags);

    if (!strncmp(src_path, dest_path, MAX_FILE_NAME_LEN)) return 0;

    if (flags == RENAME_EXCHANGE) assert(0);

    struct file_entry *src_entry = find_entry(src_path);
    struct file_entry *dest_entry = find_entry(dest_path);
    if (!src_entry) return -ENOENT;
    if (flags == RENAME_NOREPLACE && dest_entry) return -EEXIST; 
    
    // assert(IS_DIR(src_entry));
    
    if (dest_entry) del_file(dest_entry);
    free(src_entry->path);
    src_entry->path = strdup(dest_path);
    if (IS_DIR(src_entry)) {
        entry_iter *ptr;
        foreach_entry(ptr) {
            struct file_entry *src_sub_entry = get_entry(ptr);
            if (is_parent_path(src_path, src_sub_entry->path, 0)) {
                assert(0);
                char* src_sub_path = src_sub_entry->path;
                char* dest_sub_path = replace_first_after("", src_sub_path, src_path, dest_path);
                struct file_entry *dest_sub_entry = find_entry(dest_sub_path);
                if (dest_sub_entry) del_file(dest_sub_entry);
                free(src_sub_entry->path);
                src_sub_entry->path = dest_sub_path;
            }
        }
    }
    return 0;
}

static const struct fuse_operations keibot_oper = {
    .init = keibot_init,
    .destroy = keibot_destroy,
    .getattr = keibot_getattr,
    .mknod = keibot_mknod,
    .mkdir = keibot_mkdir,
    .unlink = keibot_unlink,
    .rmdir = keibot_rmdir,
    .read = keibot_read,
    .write = keibot_write,
    .readdir = keibot_readdir,
    .open = keibot_open,
    .release = keibot_release,
    .opendir = keibot_opendir,
    .releasedir = keibot_releasedir,
    .access = keibot_access,
    .utimens = keibot_utimens,
    .rename = keibot_rename,
};


int main(int argc, char *argv[]) {
    options.bot1 = strdup("aris");
    options.bot2 = strdup("kei");
    options.show_help = 0;
    options.chatbot_mode = 0;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1) return 1;
    int ret = fuse_main(args.argc, args.argv, &keibot_oper, NULL);
    fuse_opt_free_args(&args);
    return ret;
}