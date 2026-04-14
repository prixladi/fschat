typedef long ssize_t;
typedef unsigned long size_t;
typedef unsigned int mode_t;

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_NANOSLEEP 35

#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_CREAT 0x040
#define O_APPEND 0x400

#define MODE_644 0644

#define STDOUT 1
#define STDERR 2

#define bool int
#define true 1
#define false 0

#define NULL 0

struct timespec
{
    long tv_sec;
    long tv_nsec;
};

int main(int argc, char **argv);

static int create_file(const char *path);
static int read_file(const char *path);
static int read_file_follow(const char *path);
static int write_file(const char *path, const char *content);
static void print_usage(const char *exe_name, bool is_error);

static size_t strlen(const char *s);
static int streq(const char *a, const char *b);

static void fprint(int fd, const char *s);
static void fprintln(int fd, const char *s);
static void print(const char *s);
static void println(const char *s);
static void eprintln(const char *s);
static void eprint(const char *s);

static int open(const char *path, int flags, mode_t mode);
static void close(int fd);
static ssize_t read(int fd, void *buf, size_t n);
static ssize_t write(int fd, const void *buf, size_t n);
static void sleep(int secs);

static inline long sc1(long n, long a1);
static inline long sc2(long n, long a1, long a2);
static inline long sc3(long n, long a1, long a2, long a3);

void __attribute__((naked))
_start(void)
{
    __asm__("xor %%rbp, %%rbp\n"
            "pop %%rdi\n"
            "mov %%rsp, %%rsi\n"
            "and $-16,  %%rsp\n"
            "call main\n"
            "mov %%rax, %%rdi\n"
            "mov $60,   %%rax\n"
            "syscall\n"
            :);
}

int
main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage(argv[0], true);
        return 1;
    }

    if (streq(argv[1], "create"))
    {
        if (argc == 3)
            return create_file(argv[2]);
        else
        {
            eprintln("error: invalid number of arguments for create command");
            print_usage(argv[0], true);
            return 1;
        }
    }

    if (streq(argv[1], "read"))
    {
        if (argc == 3)
            return read_file(argv[2]);
        else if (argc == 4 && streq(argv[2], "-f"))
            return read_file_follow(argv[3]);
        else
        {
            eprintln("error: invalid number of arguments for read command.");
            print_usage(argv[0], true);
            return 1;
        }
    }

    if (streq(argv[1], "post"))
    {
        if (argc == 4)
            return write_file(argv[2], argv[3]);
        else
        {
            eprintln("error: invalid number of arguments for post command");
            print_usage(argv[0], true);
            return 1;
        }
    }

    if (streq(argv[1], "help") || streq(argv[1], "-h") || streq(argv[1], "--help"))
    {
        print_usage(argv[0], false);
        return 0;
    }

    eprintln("error: unknown command");
    print_usage(argv[0], true);
    return 1;
}

static void
print_usage(const char *exe_name, bool is_error)
{
    int fd = is_error ? STDERR : STDOUT;
    fprintln(fd, "usage:");

    fprint(fd, "   ");
    fprint(fd, exe_name);
    fprintln(fd, " create <path>");

    fprint(fd, "   ");
    fprint(fd, exe_name);
    fprintln(fd, " read <path>");

    fprint(fd, "   ");
    fprint(fd, exe_name);
    fprintln(fd, " read -f <file>");

    fprint(fd, "   ");
    fprint(fd, exe_name);
    fprintln(fd, " post <file> <content>");
}

static int
create_file(const char *path)
{
    int fd = open(path, O_CREAT, MODE_644);
    if (fd < 0)
    {
        eprintln("error: cannot create file");
        return 1;
    }

    return 0;
}

#define READ_BUFFER_SIZE 4096
static char read_buffer[READ_BUFFER_SIZE];
static int
read_file(const char *path)
{
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        eprintln("error: cannot open file");
        return 1;
    }

    ssize_t n;
    while ((n = read(fd, read_buffer, READ_BUFFER_SIZE)) > 0)
        write(STDOUT, read_buffer, (size_t)n);
    close(fd);
    return 0;
}

static int
read_file_follow(const char *path)
{
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        eprintln("error: cannot open file");
        return 1;
    }

    ssize_t n;
    while ((n = read(fd, read_buffer, READ_BUFFER_SIZE)) > 0)
        write(STDOUT, read_buffer, (size_t)n);

    for (; n >= 0;)
    {
        sleep(1);
        while ((n = read(fd, read_buffer, READ_BUFFER_SIZE)) > 0)
            write(STDOUT, read_buffer, (size_t)n);
    }
    close(fd);
    return 0;
}

static int
write_file(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_CREAT, MODE_644);
    if (fd < 0)
    {
        eprintln("error: cannot open file");
        return 1;
    }
    
    write(fd, content, strlen(content));
    close(fd);
    return 0;
}

static int
open(const char *path, int flags, mode_t mode)
{
    return (int)sc3(SYS_OPEN, (long)path, flags, mode);
}

static void
close(int fd)
{
    sc1(SYS_CLOSE, fd);
}

static ssize_t
read(int fd, void *buf, size_t n)
{
    return sc3(SYS_READ, fd, (long)buf, (long)n);
}

static ssize_t
write(int fd, const void *buf, size_t n)
{
    return sc3(SYS_WRITE, fd, (long)buf, (long)n);
}

static void
sleep(int secs)
{
    struct timespec ts = { secs, 0 };
    sc2(SYS_NANOSLEEP, (long)&ts, 0);
}

static size_t
strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

static int
streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

static void
fprint(int fd, const char *s)
{
    write(fd, s, strlen(s));
}

static void
fprintln(int fd, const char *s)
{
    write(fd, s, strlen(s));
    write(fd, "\n", 1);
}

static void
print(const char *s)
{
    write(STDOUT, s, strlen(s));
}

static void
println(const char *s)
{
    write(STDOUT, s, strlen(s));
    write(STDOUT, "\n", 1);
}

static void
eprint(const char *s)
{
    write(STDERR, s, strlen(s));
}

static void
eprintln(const char *s)
{
    write(STDERR, s, strlen(s));
    write(STDERR, "\n", 1);
}

static inline long
sc1(long n, long a1)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return r;
}

static inline long
sc2(long n, long a1, long a2)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return r;
}

static inline long
sc3(long n, long a1, long a2, long a3)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return r;
}
