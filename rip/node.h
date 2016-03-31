#ifndef NODE_H
#define NODE_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "node.h"
using namespace std;

#define MAX_BACK_LOG (5)
#define LOCAL_HOST ("127.0.0.1")

typedef struct sockaddr_in Sockaddr_in;
typedef struct sockaddr Sockaddr;

class Iface {
public:
	int port;
	string localAddr;
	string remoteAddr;
	Iface *next;
	int no;
	int cfd;
	Sockaddr_in *saddr;
	Iface(int p = 0, string la = "", string ra = "", int n = 0):
		port(p), localAddr(la), remoteAddr(ra), no(n){}
};

class Node {
public:
	int port;
	Iface *iface;
	int numIface;
	int sfd;
	Sockaddr_in *saddr;
	Node(int p): port(p) {}
	~Node() {
		free(saddr);
		clearIface();
	}
	void clearIface() {
		Iface *tmp = iface;
		while (iface) {
			iface = iface->next;
			free(tmp->saddr);
			delete tmp;
			tmp = iface;
		}
	}
};

#endif
