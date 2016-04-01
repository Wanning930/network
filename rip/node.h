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
using namespace std;

#define MAX_BACK_LOG (5)
#define LOCAL_HOST ("127.0.0.1")

typedef struct sockaddr_in Sockaddr_in;
typedef struct sockaddr Sockaddr;

struct Nface {
	// string localIP;
	// string remoteIP;
	int cfd;
	in_port_t port;
	bool status;
	Sockaddr_in *saddr;
	Nface(in_port_t p = 0):port(p){}
};

class Node {
public:
	int numFace;
	in_port_t port;
	vector<Nface *> it;

	Node(unsigned p): port(p) {recving = false;};
	~Node(); 
	bool startLink();
	void sendp(int id, string msg);
	void register(void (*f)(int));
	void recvp(char buffer[]);

private:
	int sfd;
	Sockaddr_in *saddr;
	pthread_t tidRecv;
	bool recving;
	void (*handler)(void *arg);

	int createConn();
	bool isGoodConn(int status);
	void delFace();
	bool startRecv();
	void killRecv();

};

#endif
