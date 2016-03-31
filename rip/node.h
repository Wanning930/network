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

class Iface {
public:
	int port;
	string localAddr;
	string remoteAddr;
	Iface *next;
	int no;
	int serverFD;
	int clientFD;
	// ina.sin_addr.s_addr = inet_addr("132.241.5.10");
	Iface(int p = 0, string la = "", string ra = "", int n = 0):
		port(p), localAddr(la), remoteAddr(ra), no(n){}
};

class Node {
public:
	int port;
	Iface *iface;
	int numIface;
	Node(int p): port(p) {}
	~Node() {
		clearIface();
	}
	void clearIface() {
		Iface *tmp = iface;
		while (iface) {
			iface = iface->next;
			delete(tmp);
			tmp = iface;
		}
	}
};

#endif
