/* router.cpp */
#include <queue>
#include "router.h"
using namespace std;

void *routerRecv(void *arg);

Router::Router(unsigned short p) {
	node = new Node(p);
	node->router = this;
	node->setHandler(routerRecv);
	pthread_mutex_init(&rtlock, NULL);
	// rtlock = PTHREAD_MUTEX_INITIALIZER;
}

Router::~Router() {
	delete node;
	delRt();
	delIt();
}

void Router::delRt() {
	pthread_mutex_lock(&rtlock);
	list<Entry *>::iterator pt = rt.begin();
	for (; pt != rt.end(); pt++) {
		delete *pt;
		*pt = NULL;
	}
	pthread_mutex_unlock(&rtlock);
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

void *routerRecv(void *a) {
	printf("handler function\n");
	char *arg = (char *)a;
	Router *router = NULL;
	memcpy(&router, arg, sizeof(void *));
	char *buf = (char *)malloc(MAX_IP_LEN);
	memcpy(buf, arg + sizeof(void *), MAX_IP_LEN);
	free(arg);

	ip_t *iph = (ip_t *)buf;
	if (router->isDest(iph->dest)) {
		if (iph->protocal == 0) {
			printf("recv: %s\n", buf + sizeof(ip_t));
		}
		else {

		}
	}
	else {
		int id = router->findIt(iph->dest);
		if (id >= 0) {
			if (!router->node->sendp(id, buf, MAX_IP_LEN)) {
				printf("cannot send: discard packet");
			}
		}
		else {
			printf("cannot routing: discard packet\n");
		}
	}
	free(buf);
	return NULL;
}

bool Router::wrapSend(int id, in_addr_t destIP, const char *msg, bool flag) {
	Rface *rf = it[id];
	in_addr_t src = it[id]->localIP;
	size_t len = strlen(msg) + sizeof(ip_t);
	char *packet = (char *)malloc(len);
	ip_t *ip = (ip_t *)packet;
	memset(packet, 0, sizeof(ip_t));
	memcpy(packet + sizeof(ip_t), msg, strlen(msg));
	ip->protocal = (flag) ? 0 : 200;
	ip->src = rf->localIP;
	ip->dest = destIP;
	printf("here\n");
	bool result = node->sendp(id, packet, len);
	free(packet);
	return result;

	/*ip->hl = sizeof(ip_t);
	ip->version = 4;
	ip->tos = 0;
	ip->length = htons(sizeof(ip_t) + strlen(msg));
	ip->iden = htons(0);
	ip->offset = htons(0);
	ip->ttl = 255;
	ip->protocal = (flag)? 0:200;
	ip->sum = 0;
	ip->src = htonl(nf->localIP);
	ip->dest = htonl(destIP);
	ip->sum = ip_sum(packet, len);*/
}

bool Router::isDest(in_addr_t dest) {
	int i = 0;
	int num = it.size();
	for (; i < num; i++) {
		if (it[i]->localIP == dest) {
			return true;
		}
	}
	return false;
}

int Router::findIt(in_addr_t dest) {
	
	pthread_mutex_lock(&rtlock);

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
		return -1;
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
		return -1;
	}
	if (!it[i]->active) {
		printf("the interface is down\n");
		return -1;
	}
	return i;
}

bool Router::send(in_addr_t dest, string longMsg) {

	printf("send '%s' to %u\n", longMsg.c_str(), dest);

	queue<string> que;
	string msg;
	if (longMsg.size() > MAX_MSG_LEN) {
		do {
			msg = longMsg.substr(0, MAX_MSG_LEN);
			que.push(msg);
			longMsg = longMsg.substr(MAX_MSG_LEN);
		} while (longMsg.size() > MAX_MSG_LEN);
	}
	else {
		que.push(longMsg);
	}

	int id = findIt(dest);
	if (id < 0) {
		printf("cannot routing\n");
		return true;
	}
	printf("hello\n");
	while (!que.empty()) {
		msg = que.front();
		que.pop();
		if (!wrapSend(id, dest, msg.c_str(), true)) {
			return false;
		}
	}
	return true;
}

bool Router::send(string d, string longMsg) {
	in_addr_t dest = inet_addr(d.c_str());
	return send(dest, longMsg);
}