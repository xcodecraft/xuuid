#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <dlfcn.h>

#include "ae.h"
#include "anet.h"

#define REDIS_VERSION "1.0"

/* Error codes */
#define REDIS_OK                0
#define REDIS_ERR               -1

/* Static server configuration */
#define REDIS_SERVERPORT        5001    /* TCP port */
#define REDIS_MAXIDLETIME       (60*5)  /* default client timeout */

/* Log levels */
#define REDIS_DEBUG 0
#define REDIS_VERBOSE 1
#define REDIS_NOTICE 2
#define REDIS_WARNING 3
#define REDIS_FATAL 4

/* Anti-warning macro... */
#define REDIS_NOTUSED(V) ((void) V)
#define redisAssert assert

#define READ_BUF_LEN 64
#define WRITE_BUF_LEN 512

/* Global server state structure */
struct RedisServer {
    pthread_t mainthread;
    unsigned int clients;
    int port;
    int fd;
    char neterr[ANET_ERR_LEN];
    aeEventLoop *el;
    time_t stat_starttime;         /* server start time */
    long long stat_numcommands;    /* number of processed commands */
    long long stat_numconnections; /* number of connections received */
    int verbosity;
    int maxidletime;
    int daemonize;
    char *pidfile;
    char *logfile;
    char *bindaddr;
    unsigned int maxclients;
    time_t unixtime;    /* Unix time sampled every second. */
};
typedef struct RedisServer redisServer;

struct RedisClient {
	int fd;
	char rbuf[READ_BUF_LEN];
	char wbuf[WRITE_BUF_LEN];
	int rlen;
	int wlen;
	int wpos;
	time_t lastinteraction;
};
typedef struct RedisClient redisClient;

static redisClient *createClient(int fd);
static void redisLog(int level, const char *fmt, ...);
static void acceptHandler(aeEventLoop *el, int fd, void *privdata, int mask);
static void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask);
static void processInputBuffer(redisClient *c);
static void initServerConfig() ;
static void initServer();
static void setupSigSegvAction(void);
static void version() ;
static void usage();
static void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask);
static void sigtermHandler(int sig);
static void segvHandler(int sig, siginfo_t *info, void *secret);
static void freeClient(redisClient *c);
static void resetClient(redisClient *c);
static void oom(const char *msg);

static redisServer server;

static void redisLog(int level, const char *fmt, ...) {
    if (level < server.verbosity) return;
    va_list ap;
    FILE *fp;
    char *c = ".-*#";
    char buf[64];
    time_t now;
    fp = (server.logfile == NULL) ? stdout : fopen(server.logfile,"a");
    if (!fp) return;
    va_start(ap, fmt);
    now = time(NULL);
    strftime(buf,64,"%d %b %H:%M:%S",localtime(&now));
    fprintf(fp,"[%d] %s %c ",(int)getpid(),buf,c[level]);
    vfprintf(fp, fmt, ap);
    fprintf(fp,"\n");
    fflush(fp);
    va_end(ap);
    if (server.logfile) fclose(fp);
}

static void oom(const char *msg) {
    redisLog(REDIS_WARNING, "%s: Out of memory\n",msg);
    sleep(1);
    abort();
}

static void acceptHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd;
    char cip[128];
    redisClient *c;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(privdata);
    REDIS_NOTUSED(mask);
    cfd = anetAccept(server.neterr, fd, cip, &cport);
    if (cfd == AE_ERR) {
        if(server.verbosity <= REDIS_VERBOSE )
            redisLog(REDIS_VERBOSE,"Accepting client connection: %s", server.neterr);
        return;
    }
    if(server.verbosity <= REDIS_VERBOSE )
        redisLog(REDIS_VERBOSE,"Accepted %s:%d", cip, cport);
    if ((c = createClient(cfd)) == NULL) {
        redisLog(REDIS_WARNING,"Error allocating resoures for the client");
        close(cfd); /* May be already closed, just ingore errors */
        return;
    }
    server.stat_numconnections++;
    if (server.maxclients && server.clients > server.maxclients) {
        char *err = "-ERR max number of clients reached\r\n";
        /* That's a best effort error message, don't check write errors */
        if (write(c->fd,err,strlen(err)) == -1) {
            /* Nothing to do, Just to avoid the warning... */
        }
        freeClient(c);
        return;
    }
}

static redisClient *createClient(int fd) {
    redisClient *c = calloc(1,sizeof(redisClient));
    if (!c) return NULL;
    anetNonBlock(NULL,fd);
    anetTcpNoDelay(NULL,fd);
    if (aeCreateFileEvent(server.el,fd,AE_READABLE,readQueryFromClient, c) == AE_ERR) {
        close(fd);
        free(c);
        return NULL;
    }
    c->fd = fd;
    server.clients++;
    return c;
}

static void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    redisClient *c = (redisClient*) privdata;
    char *buf;
    int nread;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    buf=c->rbuf+c->rlen;

    nread = read(fd, buf, READ_BUF_LEN- c->rlen -1);
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            if(server.verbosity <= REDIS_VERBOSE )
                redisLog(REDIS_VERBOSE, "Reading from client: %s",strerror(errno));
            freeClient(c);
            return;
        }
    } else if (nread == 0) {
        if(server.verbosity <= REDIS_VERBOSE )
            redisLog(REDIS_VERBOSE, "Client closed connection");
        freeClient(c);
        return;
    }
    if (nread) {
        c->rlen+=nread;
        if(c->rlen >= READ_BUF_LEN - 1) {
            freeClient(c);
            return;
        }
        c->lastinteraction = time(NULL);
    } else {
        return;
    }
    processInputBuffer(c);
}

static void setupSigSegvAction(void) {
    struct sigaction act;
    sigemptyset (&act.sa_mask);
    /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction
     * is used. Otherwise, sa_handler is used */
    act.sa_flags = SA_NODEFER | SA_ONSTACK | SA_RESETHAND | SA_SIGINFO;
    act.sa_sigaction = segvHandler;
    sigaction (SIGSEGV, &act, NULL);
    sigaction (SIGBUS, &act, NULL);
    sigaction (SIGFPE, &act, NULL);
    sigaction (SIGILL, &act, NULL);
    sigaction (SIGBUS, &act, NULL);

    act.sa_flags = SA_NODEFER | SA_ONSTACK | SA_RESETHAND;
    act.sa_handler = sigtermHandler;
    sigaction (SIGTERM, &act, NULL);
    return;
}

static void sigtermHandler(int sig) {
    REDIS_NOTUSED(sig);
    redisLog(REDIS_WARNING,"SIGTERM received, scheduling shutting down...");
    _exit(0);
}

static void segvHandler(int sig, siginfo_t *info, void *secret) {
    REDIS_NOTUSED(info);
    REDIS_NOTUSED(secret);
    redisLog(REDIS_WARNING,
        "======= Ooops! Redis %s got signal: -%d- =======", REDIS_VERSION, sig);
    _exit(0);
}

static void freeClient(redisClient *c) {
    aeDeleteFileEvent(server.el,c->fd,AE_READABLE);
    aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
    close(c->fd);
    if(c) {
        free(c);
        c=NULL;
    }
    server.clients--;
}

static void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    redisClient *c = privdata;
    int nwritten = 0;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    nwritten = write(fd, c->wbuf + c->wpos, c->wlen - c->wpos);
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else {
            if(server.verbosity <= REDIS_VERBOSE )
                redisLog(REDIS_VERBOSE,
                    "Error writing to client: %s", strerror(errno));
            freeClient(c);
            return;
        }
    }
    if (nwritten > 0) {
    	c->lastinteraction = time(NULL);
    	c->wpos += nwritten;
    }
    if (c->wpos == c->wlen) {
        resetClient(c);
    }
}

static void resetClient(redisClient *c) {
    memset(c->rbuf,0,READ_BUF_LEN);
    memset(c->wbuf,0,WRITE_BUF_LEN);
    c->rlen=0;
    c->wpos=0;
    c->wlen=0;
    aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
    aeDeleteFileEvent(server.el,c->fd,AE_READABLE);
    aeCreateFileEvent(server.el, c->fd, AE_READABLE,
        readQueryFromClient, c);
}

static void initServerConfig() {
    server.port = REDIS_SERVERPORT;
    server.verbosity = REDIS_FATAL ;
    server.maxidletime = REDIS_MAXIDLETIME;
    server.logfile = NULL; /* NULL = log on standard output */
    server.bindaddr = NULL;
    server.daemonize = 0;
    server.pidfile = "/var/run/redis.pid";
    server.maxclients = 30720;
}

static void initServer() {
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    setupSigSegvAction();

    server.mainthread = pthread_self();
    server.clients = 0;
    server.el = aeCreateEventLoop();
    server.fd = anetTcpServer(server.neterr, server.port, server.bindaddr);
    if (server.fd == -1) {
        redisLog(REDIS_WARNING, "Opening TCP port: %s", server.neterr);
        exit(1);
    }
    server.stat_numcommands = 0;
    server.stat_numconnections = 0;
    server.stat_starttime = time(NULL);
    server.unixtime = time(NULL);
    if (aeCreateFileEvent(server.el, server.fd, AE_READABLE,
        acceptHandler, NULL) == AE_ERR) oom("creating file event");

}

// =====================================================================================
// ------------------------------------- uuid logic ------------------------------------
// =====================================================================================

unsigned long long get_uuid();
int ProcessRequestBuffer(redisClient *c);

/**
 * offset
 */
unsigned int ID_OFFSET = 1251414000;
/*
 * bit length_BIT_LENGTH
 */
unsigned int SEQ_BIT_LENGTH = (18);
unsigned int INSTANCE_SEQ_BIT_LENGTH = (6 + 18);
/*
 * seq bit value
 */
unsigned int SEQ_BIT_VALUE = (1 << 18);

unsigned int instance;
unsigned int seq;
unsigned long long get_uuid() {
    unsigned long long uuid= (unsigned long long)time(NULL);
    uuid -= ID_OFFSET;
    uuid <<= INSTANCE_SEQ_BIT_LENGTH;

    uuid += (instance << SEQ_BIT_LENGTH);
    seq++;
    uuid += (seq % SEQ_BIT_VALUE);
    return uuid;
}
int ProcessRequestBuffer(redisClient *c) {
    char *rbuf=c->rbuf;
    int rlen=c->rlen;
    char *p=strstr(rbuf,"\r\n");
    if( p == NULL) {
        return 0;
    } else if(p != rbuf+rlen-2 ) {
        return -1;
    }
    if(strcasecmp(rbuf,"quit\r\n") == 0) { // client ask to quit
        return -2;
    }
    if(strncasecmp(rbuf,"get ",4) == 0 ) {
        snprintf(c->wbuf,WRITE_BUF_LEN-1,"VALUE uuid 0 16\r\n%llu\r\nEND\r\n",get_uuid());
        c->wlen=strlen(c->wbuf);
        server.stat_numcommands++;
        return 1;
    }
    if(strncasecmp(rbuf,"stats ",4) == 0 ) {
        snprintf(c->wbuf,WRITE_BUF_LEN-1,"STAT version %s\r\nSTAT instance_id %u\r\nSTAT cmd_get %lld\r\nSTAT curr_connections %u\r\nSTAT total_connections %lld\r\nEND\r\n",REDIS_VERSION,instance,server.stat_numcommands,server.clients,server.stat_numconnections);
        c->wlen=strlen(c->wbuf);
        return 1;
    }
    return -1;
}
static void processInputBuffer(redisClient *c) {
    int v=ProcessRequestBuffer(c);
    if(v == -1) { // client protocol error
        freeClient(c);
        return;
    }
    if(v == -2) { //QUIT command
        freeClient(c);
        return;
    }
    if(v == 0) { //not enough data
        return;
    }
    if(v == 1) { //data is ok,now send reply to client
        aeDeleteFileEvent(server.el,c->fd,AE_READABLE);
        if (aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,
            sendReplyToClient, c) == AE_ERR) return;
    }
}

static void version() {
    printf("uuid server version %s\n", REDIS_VERSION);
    return;
}
static void usage(void) {
    version();
    printf("-p <num>      TCP port number to listen on (default: 5001)\n"
           "-l <ip_addr>  interface to listen on (default: INADDR_ANY, all addresses)\n"
           "-d            run as a daemon\n"
           "-c <num>      max simultaneous connections (default: 1024)\n"
           "-v            verbose (print errors/warnings while in event loop)\n"
           "-i <num>      the instance id, must be a number between 0 and 15\n"
           );
    return;
}

int main(int argc, char **argv) {
    REDIS_NOTUSED(argc);
    REDIS_NOTUSED(argv);
    initServerConfig();
    instance=0; // ??
    seq=0;
    int c;
    /* process arguments */
    while (-1 != (c = getopt(argc, argv,
          "p:"  /* TCP port number to listen on */
          "c:"  /* max simultaneous connections */
          "i:"  /* instance id */
          "v"   /* verbose */
          "d"   /* daemon mode */
          "h"   /* show usage */
          "l:"  /* interface to listen on */
        ))) {
        switch (c) {
        case 'p':
            server.port= atoi(optarg);
            break;
        case 'c':
            server.maxclients= atoi(optarg);
            break;
        case 'i':
            instance= atoi(optarg);
            if (instance < 0 || instance > 15) {
                fprintf(stderr, "instance must be a number between 0 and 15.\n");
                return 1;
            }
            break;
        case 'v':
            server.verbosity = REDIS_DEBUG;
            break;
        case 'd':
            server.daemonize = 1;
            break;
        case 'h':
            usage();
            return 1;
            break;
        case 'l':
            server.bindaddr = strdup(optarg);
            break;
        default:
            fprintf(stderr, "Illegal argument \"%c\"\n", c);
            usage();
            return 1;
        }
    }
    if (instance < 0 || instance > 15) {
        fprintf(stderr, "instance must be a number between 0 and 15.\n");
        return 1;
    }

    if (server.daemonize) daemon(0,0);
    redisLog(REDIS_NOTICE,"Server started, Redis version " REDIS_VERSION);
    initServer();
    redisLog(REDIS_NOTICE,"The server is now ready to accept connections on port %d", server.port);
    aeMain(server.el);
    aeDeleteEventLoop(server.el);
    return 0;
}
