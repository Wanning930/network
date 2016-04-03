/* router.cpp */
#include "router.h"
using namespace std;

Router::Router(unsigned short p) {
	node = new Node(p);
	node->router = this;
}

Router::~Router() {
	delete node;
	delRt();
	delIt();
}

void Router::delRt() {
	list<Entry *>::iterator pt = rt.begin();
	for (; pt != rt.end(); pt++) {
		delete *pt;
		*pt = NULL;
	}
}

void Router::delIt() {
	int num = it.size();
	int i = 0;
	for (; i < num; i++) {
		delete it[i];
		it[i] = NULL;
	}
}

void Router::setActive(int id, bool flag) {
	it[id]->active = flag;
}

bool send(void *arg) {
	Router *router = *((Router **)*arg);
	in_addr_t dest = *((in_addr_t *)*(arg + 1));
	char *msg = *((char *)*(arg + 2));
	return router->send(dest, msg.to_string());
}

bool Router::send(in_addr_t dest, string msg) {

	printf("send '%s' to %d\n", msg.to_string(), dest);

	list<Entry *>::iterator pt = rt.begin();
	in_addr_t next = 0;
	for(; pt != rt.end(); pt++) {
		if ((*pt)->destIP == dest) {
			next = (*pt)->nextIP;
			break;
		}
	}
	if (pt == rt.end()) {
		printf("no such destination in routing table\n");
		return false;
	}

	int interface = 0;
	int inum = it.size();
	int i = 0;
	for (; i < inum; i++) {
		if (it[i]->nextIP == next) {
			break;
		}
	}
	if (i == inum) {
		printf("rt and it not match\n");
		return false;
	}
	if (!it[i]->active) {
		printf("the interface is down\n");
		return true;
	}

	return node->sendp(i, msg);
}

bool Router::send(string d, string msg) {
	in_addr_t dest = inet_addr(d.c_str());
	return send(dest, msg);
}