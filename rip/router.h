#ifndef ROUTER_H
#define ROUTER_H

/* router.h */
#include <map>
#include <list>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include "node.h"
using namespace std;

struct Entry {
	in_addr_t destIP;
	int cost;
	in_addr_t nextIP;
	int timeStamp;
	Entry(string a, int b, string c): destIP(inet_addr(a.c_str())), cost(b), nextIP(inet_addr(c.c_str())), timeStamp(0) {};
	Entry(in_addr_t a, int b, in_addr_t c): destIP(a), cost(b), nextIP(c), timeStamp(0) {};
};

struct Rface {
	in_addr_t localIP;
	in_addr_t nextIP;
	bool active;
	Rface(in_addr_t a, in_addr_t b): localIP(a), nextIP(b), active(true) {};
	Rface(string a, string b): localIP(inet_addr(a.c_str())), nextIP(inet_addr(b.c_str())), active(true) {};
};

class Router {
public:
	list<Entry *> rt;
	pthread_mutex_t rtlock;
	vector<Rface *> it; // [(local ip, next hop ip)]
	Node *node;
	timer_t timerid;

	Router(in_port_t p);
	~Router();
	bool send(in_addr_t dest, string longMsg);
	bool send(string dest, string msg);
	bool isDest(in_addr_t dest);
	int findIt(in_addr_t dest);
	bool recvRip(char *buf);
	bool startTimer();
	bool checkTimer();
	void setActive(int id, bool flag);

private:
	int timeStamp;

	void delRt();
	void delIt();
	bool wrapSend(in_addr_t dest, const char *msg, bool flag);
	bool rtUpdate(in_addr_t dest, in_addr_t src, int cost);
	bool sendRip(bool flag);
};

#endif