#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#define PORT "9034"

#define CMD_OK 0
#define CMD_ERROR_PEOPLE_LIST_EMPTY -1
#define CMD_ERROR_COMMAND_NOT_FOUND -2
#define CMD_ERROR_PERSON_NOT_FOUND -3
#define CMD_ERROR_PERSON_ALREADY_LOGGED_IN -4
#define CMD_ERROR_INVALID_ARGUMENT -5
#define CMD_OK_EXIT -65533
#define CMD_ERROR_OUT_OF_MEMORY -65534
#define CMD_ERROR_UNKNOWN -65535

#define BLOCK_PERIOD 1 
#define ROUND_PERIOD 30 
#define INITIAL_HEALTH_POINT 10 


#ifndef IF_EQUAL_RETURN
#define IF_EQUAL_RETURN(param, val) if(val == param) return #val
#endif

typedef struct {
    char *name;
    int iSocketDescriptor;
    int hp;
    int heatPoints;
    int killer;
    int cmdBytes;
    char cmd[1000];
} PERSON;

PERSON *persons = NULL;
unsigned int personsCount = 0;

#define CMD_LOGIN "login" 
#define CMD_LOGOUT "logout" 
#define CMD_WHO "who" 
#define CMD_WALL "wall" 
#define CMD_SAY "say" 
#define CMD_KILL "kill" 
#define CMD_HEAL "heal" 
#define CMD_SHUTDOWN "shutdown"

#define ARRAYLENGTH(_array) (sizeof(_array) / sizeof(_array[0]))

int checkPersonName(const char *personName) {
    if (personName == NULL) return CMD_ERROR_INVALID_ARGUMENT;
    int len = strlen(personName);
    if (len < 1 || len > 10) return CMD_ERROR_INVALID_ARGUMENT;
    int iResult = CMD_OK;
    unsigned char i = 0;
    for (i = 0; i < strlen(personName); ++i) {
        if (!isalpha(personName[i])) {
            iResult = CMD_ERROR_INVALID_ARGUMENT;
        }
    }
    return iResult;
}

void sendChArr(const int iSocketDescriptor, const char *s, int size) {
    if (size > 0) {
        int total = 0; 
        int bytesleft = size; 
        int n;
        while (total < size) {
            n = send(iSocketDescriptor, s + total, bytesleft, 0);
            if (n == -1) { break; }
            total += n;
            bytesleft -= n;
        }
        perror("send");
    }
}

void *sendStr(const int iSocketDescriptor, const char *s) {
    printf("sendStr iSocketDescriptor=%d text=%s \n\r", iSocketDescriptor, s);
    sendChArr(iSocketDescriptor, s, strlen(s));
}


int implWho(const int iSocketDescriptor) {
    int iResult = CMD_OK;
    unsigned int i = 0, il = personsCount;
    if (il == 0) sendStr(iSocketDescriptor, "Person list empty\n");
    char buf[10000];
    char s[1000];
    buf[0] = '\0';
    for (; i < il; ++i) {
        if (persons[i].name != NULL) {
            printf("%d: %s\n", i, persons[i].name);
            if (persons[i].iSocketDescriptor == iSocketDescriptor) {
                int res = snprintf(s, sizeof(s), "%d: %s %d - you\n\r", i, persons[i].name, persons[i].hp);
            } else {
                int res = snprintf(s, sizeof(s), "%d: %s %d\n\r", i, persons[i].name, persons[i].hp);
            }
            strcat(buf, s);
        }
    }
    sendStr(iSocketDescriptor, buf);
    return iResult;
}

int implLogin(const int iSocketDescriptor, const char *personName) {
    int iResult = CMD_OK;
    if (checkPersonName(personName) == CMD_OK) {
        if (findPersonByName(personName) < 0) {
            int iPersonIndex = findPersonBySocketDescriptor(iSocketDescriptor);
            int sz = strlen(personName);
            persons[iPersonIndex].name = malloc(sz + 1);
            memcpy(persons[iPersonIndex].name, personName, sz + 1);
        } else {
            sendStr(iSocketDescriptor, "user already exists \r\nlogin: ");
        }
    } else {
        sendStr(iSocketDescriptor, "wrong name \r\nlogin: ");
        iResult = CMD_ERROR_INVALID_ARGUMENT;
    };
    return iResult;
}


int implWall(const int iSocketDescriptor, const char *text) {
    int iResult = CMD_OK;
    if (text == NULL || strlen(text) == 0) {
        sendStr(iSocketDescriptor, "ERROR: Empty shout is forbidden\n\r");
    } else {
        unsigned int i = 0, il = personsCount;
        char s[1000];
        int iPersonIndex = findPersonBySocketDescriptor(iSocketDescriptor);
        int res = snprintf(s, sizeof(s), "%s shouts: %s\n\r", persons[iPersonIndex].name, text);
        for (; i < il; ++i) {
            if (persons[i].iSocketDescriptor != iSocketDescriptor && persons[i].name != NULL) {
                sendStr(persons[i].iSocketDescriptor, s);
            }
        }
    }
    return iResult;
}

void *sendAll(const char *text, int excl) {
    unsigned int i = 0, il = personsCount;
    for (; i < il; ++i) {
        if (persons[i].name != NULL && i != excl) {
            sendStr(persons[i].iSocketDescriptor, text);
        }
    }
}


int implSay(const int iSocketDescriptor, const char *personName, const char *text) {
    int iResult = CMD_OK;
    char s[1000];
    if (text == NULL || strlen(text) == 0) {
        sendStr(iSocketDescriptor, "ERROR: Empty message is forbidden\n\r");
    } else {
        int iPersonIndexSrc = findPersonBySocketDescriptor(iSocketDescriptor);
        int iPersonIndexDest = findPersonByName(personName);
        int res;
        if (iPersonIndexDest >= 0) {
            res = snprintf(s, sizeof(s), "%s say: %s\n\r", persons[iPersonIndexSrc].name, text);
            sendStr(persons[iPersonIndexDest].iSocketDescriptor, s);
        } else {
            res = snprintf(s, sizeof(s), "ERROR: User %s is unknown\n\r", personName);
            sendStr(iSocketDescriptor, s);
        }
    }
    return iResult;
}


int implKill(const int iSocketDescriptor, const char *personName) {
    int iResult = CMD_OK;
    char s[1000];
    int iPersonIndexSrc = findPersonBySocketDescriptor(iSocketDescriptor);
    int iPersonIndexDest = findPersonByName(personName);
    int res;
    if (iPersonIndexDest >= 0) {
        int delta = rand() % 10 + 1;
        persons[iPersonIndexDest].heatPoints += delta;
        persons[iPersonIndexDest].killer = iPersonIndexSrc;
        res = snprintf(s, sizeof(s), "%s attacks you\n\r", persons[iPersonIndexSrc].name,
                       persons[iPersonIndexDest].hp);
        sendStr(persons[iPersonIndexDest].iSocketDescriptor, s);
    } else {
        res = snprintf(s, sizeof(s), "ERROR: User %s is unknown\n\r", personName);
        sendStr(iSocketDescriptor, s);
    }
    return iResult;
}


int implHeal(const int iSocketDescriptor, const char *personName) {
    int iResult = CMD_OK;
    char s[1000];
    int iPersonIndexSrc = findPersonBySocketDescriptor(iSocketDescriptor);
    int iPersonIndexDest = findPersonByName(personName);
    int res;
    if (iPersonIndexDest >= 0) {
        int delta = rand() % 10 + 1;
        persons[iPersonIndexDest].hp += delta;
        res = snprintf(s, sizeof(s), "%s heal you (you health=%dhp)\n\r", persons[iPersonIndexSrc].name,
                       persons[iPersonIndexDest].hp);
    } else {
        res = snprintf(s, sizeof(s), "ERROR: User %s is unknown\n\r", personName);
        sendStr(iSocketDescriptor, s);
    }
    return iResult;
}

void *turnRound() {
    if (personsCount > 0) {
        int i = personsCount - 1;
        char s[1000];
        for (; i >= 0; --i) {
            persons[i].hp -= persons[i].heatPoints;
            persons[i].heatPoints = 0;
            if (persons[i].hp <= 0) {
                persons[i].hp = INITIAL_HEALTH_POINT;
                persons[i].heatPoints = 0;
                sendStr(persons[i].iSocketDescriptor, "\n\rYou are killed\n\rlogin: ");
                int res = snprintf(s, sizeof(s), "%s is died\n\r", persons[i].name);
                sendStr(persons[persons[i].killer].iSocketDescriptor, s);
                persons[i].name = NULL;
                sendAll("Somebody is dead. R.I.P\n\r", persons[i].killer);
                persons[i].killer = 0;
            }

        }
    };
    if (personsCount > 0) {
        sendAll("\n\rNew round\n\r", -1);
    }
}


int addPerson(int iSocketDescriptor, const struct sockaddr_storage *remoteAddr) {
    int iResult = CMD_OK;
    do {
        PERSON *tmpPersons = NULL;
        unsigned int tmpPersonsCount = 0U;

        if (persons == NULL) {
            tmpPersonsCount = 1U;
            tmpPersons = malloc(sizeof(PERSON));
        } else {
            tmpPersonsCount = (personsCount + 1U);
            tmpPersons = realloc(persons, tmpPersonsCount * sizeof(PERSON));
        }

        if (tmpPersons == NULL) {
            iResult = CMD_ERROR_OUT_OF_MEMORY;
            break;
        }

        persons = tmpPersons;
        personsCount = tmpPersonsCount;

        persons[personsCount - 1].iSocketDescriptor = iSocketDescriptor;
        persons[personsCount - 1].name = NULL;
        persons[personsCount - 1].hp = INITIAL_HEALTH_POINT;
        persons[personsCount - 1].heatPoints = 0;
        persons[personsCount - 1].cmdBytes = 0;

        printf("New connection from on socket %d\n", iSocketDescriptor);
        sendStr(iSocketDescriptor, "login: ");

    } while (0);

    return iResult;
}

int removePerson(unsigned int personIndex) {
    int iResult = CMD_OK;

    do {
        unsigned int i = personIndex, il = personsCount;

        if (personIndex >= il) {
            iResult = CMD_ERROR_INVALID_ARGUMENT;
            break;
        }

        free(persons[personIndex].name);

        for (; i < il - 1; ++i) {
            persons[i] = persons[i + 1];
        }

        unsigned int tmpPersonsCount = (il - 1U);
        PERSON *tmpPersons = realloc(persons, tmpPersonsCount * sizeof(PERSON));

        if (tmpPersons == NULL) {
            iResult = CMD_ERROR_OUT_OF_MEMORY;
            break;
        }

        persons = tmpPersons;
        personsCount = tmpPersonsCount;
    } while (0);

    return iResult;
}

int findPersonByName(const char *personName) {
    int iResult = -1;
    unsigned int i = 0, il = personsCount;
    for (; i < il; ++i) {
        if (persons[i].name != NULL && strcasecmp(personName, persons[i].name) == 0) {
            iResult = i;
            break;
        }
    }
    return iResult;
}

int findPersonBySocketDescriptor(int iSocketDescriptor) {
    int iResult = -1;
    unsigned int i = 0, il = personsCount;

    for (; i < il; ++i) {
        if (iSocketDescriptor == persons[i].iSocketDescriptor) {
            iResult = i;
            break;
        }
    }
    return iResult;
}


int executeCommand(int iSocketDescriptor, const char *cmdFull, const char *cmd, const char *personName) {
    int cmdResult;
    char *operand;
    char *istr;
    do {
        unsigned int iPersonIdex = findPersonBySocketDescriptor(iSocketDescriptor);
        if (iPersonIdex >= 0) {
            if (persons[iPersonIdex].name == NULL) {
                cmdResult = implLogin(iSocketDescriptor, cmd);
            } else if (strcasecmp(cmd, CMD_KILL) == 0) {
                cmdResult = implKill(iSocketDescriptor, personName);
            } else if (strcasecmp(cmd, CMD_HEAL) == 0) {
                cmdResult = implHeal(iSocketDescriptor, personName);
            } else if (strcasecmp(cmd, CMD_SAY) == 0) {
                istr = strstr(cmdFull, personName);
                operand = istr + strlen(personName) + 1;
                operand[strcspn(operand, "\r\n")] = '\0';
                operand[strcspn(operand, "\n")] = '\0';
                cmdResult = implSay(iSocketDescriptor, personName, operand);
            } else if (strcasecmp(cmd, CMD_WALL) == 0) {
                istr = strstr(cmdFull, cmd);
                operand = istr + strlen(cmd) + 1;
                operand[strcspn(operand, "\r\n")] = '\0';
                operand[strcspn(operand, "\n")] = '\0';
                cmdResult = implWall(iSocketDescriptor, operand);
            } else if (strcasecmp(cmd, CMD_WHO) == 0) {
                cmdResult = implWho(iSocketDescriptor);
            } else if (strcasecmp(cmd, CMD_SHUTDOWN) == 0) {
                cmdResult = CMD_OK_EXIT;
            } 
        }
    } while (0);

    return cmdResult;
}

void freePersons() {
    unsigned int i = 0, il = personsCount;

    for (; i < il; ++i) {
        free(persons[i].name);
    }

    free(persons);
}

const char *getErrorName(int iErrorCode) {
    IF_EQUAL_RETURN(iErrorCode, CMD_ERROR_COMMAND_NOT_FOUND);
    IF_EQUAL_RETURN(iErrorCode, CMD_ERROR_OUT_OF_MEMORY);
    IF_EQUAL_RETURN(iErrorCode, CMD_ERROR_PEOPLE_LIST_EMPTY);
    IF_EQUAL_RETURN(iErrorCode, CMD_ERROR_PERSON_ALREADY_LOGGED_IN);
    IF_EQUAL_RETURN(iErrorCode, CMD_ERROR_PERSON_NOT_FOUND);
    IF_EQUAL_RETURN(iErrorCode, CMD_ERROR_INVALID_ARGUMENT);
    IF_EQUAL_RETURN(iErrorCode, CMD_ERROR_UNKNOWN);

    return NULL;
}

fd_set master; 
fd_set read_fds; 

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

int main() {
    srand(time(NULL));
    int iResult = EXIT_SUCCESS;
    int loopExit = 0;
    int cmdResult = CMD_OK;
    char sCmd[100];
    sCmd[0] = '\0';
    char sPersonName[100];
    sPersonName[0] = '\0';
    char sOperand[100];
    sOperand[0] = '\0';

    fd_set master; 
    fd_set read_fds; 
    int fdmax; 
    int listener; 
    int newfd; 
    struct sockaddr_storage remoteaddr; 
    socklen_t addrlen;
    char buf[1000]; 
    char sGetCmd[1000];
    int nbytes;
    char remoteIP[INET6_ADDRSTRLEN];
    int yes = 1; 
    int i, j, rv;
    struct addrinfo hints, *ai, *p;
    FD_ZERO(&master); 
    FD_ZERO(&read_fds);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "MUD: failed to bind\n");
        exit(2);
    }
    freeaddrinfo(ai); 
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }
    FD_SET(listener, &master);
    fdmax = listener; 
    int jj = 0;
    struct timeval tv;
    tv.tv_sec = BLOCK_PERIOD;
    tv.tv_usec = 0;
    time_t start = time(NULL);
    for (;;) {
        read_fds = master; 
        if (select(fdmax + 1, &read_fds, NULL, NULL, &tv) == -1) {
            perror("select");
            exit(4);
        }
        for (i = 0; i <= fdmax; ++i) {
            if (FD_ISSET(i, &read_fds)) { 
                if (i == listener) {
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                                   (struct sockaddr *) &remoteaddr,
                                   &addrlen);
                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master); 
                        if (newfd > fdmax) { 
                            fdmax = newfd;
                        }
                        addPerson(newfd, &remoteaddr);
                    }
                } else {
                    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        if (nbytes == 0) {
                            printf("socket %d hung up\n", i);
                            int iPersonIndex = findPersonBySocketDescriptor(i);
                            removePerson(iPersonIndex);
                        } else {
                            perror("recv");
                        }
                        close(i);
                        FD_CLR(i, &master); 
                    } else {
                        if (nbytes > 0 && i != listener) {
                            int personIdx = findPersonBySocketDescriptor(i);
                            char *a = persons[personIdx].cmd;
                            if (personIdx >= 0) {
                                int j = 0;
                                while (j < nbytes) {
                                    if (buf[j] == '\n' || buf[j] == '\r') {
                                        sCmd[0] = sPersonName[0] = sOperand[0] = '\0';
                                        memcpy(sGetCmd, persons[personIdx].cmd, persons[personIdx].cmdBytes);
                                        sGetCmd[persons[personIdx].cmdBytes] = '\0';
                                        printf("cmd %d: %s \n", i, sGetCmd);
                                        int ns = sscanf(sGetCmd, "%100s %100s %100s", sCmd, sPersonName, sOperand);
                                        printf("sCmd=[%s] sPersonName=[%s] sOperand=[%s]\n", sCmd, sPersonName,
                                               sOperand);
                                        if (strlen(sCmd) > 0) {
                                            cmdResult = executeCommand(i, sGetCmd, sCmd, sPersonName);
                                        }
                                        persons[personIdx].cmdBytes = 0;
                                    } else {
                                        a[persons[personIdx].cmdBytes++] = buf[j];
                                    }
                                    j++;
                                }
                            }
                        } else cmdResult = CMD_ERROR_UNKNOWN;

                        if (CMD_OK_EXIT == cmdResult) break;

                        if (CMD_OK != cmdResult) {
                            printf("Error executing command (code = %d): %s\n", cmdResult, getErrorName(cmdResult));
                        }
                    }
                } 
            } 
        } 
        time_t current = time(NULL);
        double diff = difftime(current, start);
        if (diff >= ROUND_PERIOD) {
            turnRound();
            start = time(NULL);
        }
    }

    return iResult;
}
