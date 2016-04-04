#ifndef NODE_H
#define NODE_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "node.h"
#include "protocal.h"
using namespace std;

#define MAX_BACK_LOG (5)
#define LOCAL_HOST ("127.0.0.1")
#define MAX_MSG_LEN (1405)
#define MAX_IP_LEN (1405)
// #define MAX_IP_LEN (64*1024)
#define ADDR_LEN (sizeof(struct sockaddr_in))

typedef struct sockaddr_in Sockaddr_in;
typedef struct sockaddr Sockaddr;

class Router;

struct Nface {
	int cfd;
	in_port_t port;
	Sockaddr_in *saddr;
	Nface(in_port_t p):port(p) {}
};

class Node {
public:
	int numFace;
	in_port_t port;
	vector<Nface *> it;
	void *router;

	Node(unsigned p): port(p) {recving = false;}
	~Node(); 
	bool startLink();
	bool sendp(int id, char *pkt, size_t len);
	void setHandler(void *(*f)(void *arg));
	bool recvp(char buffer[]);

private:
	int sfd;
	Sockaddr_in *saddr;
	pthread_t tidRecv;
	bool recving;
	void *(*handler)(void *arg);

	int createConn();
	bool isGoodConn(int status);
	void delFace();
	bool startRecv();
	void killRecv();
	bool startTimer();

};

#endif
