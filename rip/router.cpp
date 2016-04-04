/* router.cpp */
#include <queue>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "router.h"
using namespace std;

#define CLOCKID CLOCK_REALTIME  

void *routerRecv(void *arg);

void timer_thread(union sigval v) {  
	Router *router = (Router *)v.sival_ptr;
    printf("jwn timer! %d\n", router->port);  
}

Router::Router(unsigned short p) {
	node = new Node(p);
	node->router = this;
	node->setHandler(routerRecv);
	pthread_mutex_init(&rtlock, NULL);
}

Router::~Router() {
	delete node;
	delRt();
	delIt();
	timer_delete(timerid);
}

bool Router::startTimer() {
	
	struct sigevent evp;
	memset(&evp, 0, sizeof(struct sigevent));

	evp.sigev_notify = SIGEV_THREAD;
	evp.sigev_notify_function = timer_thread; 
	evp.sigev_value.sival_ptr = this;
    if (timer_create(CLOCKID, &evp, &timerid) == -1) {  
        perror("fail to timer_create");  
        return false;  
    }  

	struct itimerspec it;  
    it.it_interval.tv_sec = 2;  
    it.it_interval.tv_nsec = 0;  
    it.it_value.tv_sec = 1;  
    it.it_value.tv_nsec = 0;  
	if (timer_settime(timerid, 0, &it, NULL) == -1) {
		perror("fail to timer_settime"); 
		return false;
	}
	return true;
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
	bool result = false;

	pthread_mutex_lock(&rtlock);
	list<Entry *>::iterator pt = rt.begin();
	for (; pt != rt.end(); pt++) {
		if ((*pt)->nextIP == it[id]->nextIP) {
			if (flag) {
				// send a single rip entry, but not in entry table
			}
			else {

			}
		}
	}
	pthread_mutex_unlock(&rtlock);

	if (result) {
		sendRip();
	}
}

bool Router::rtUpdate(in_addr_t dest, in_addr_t src, int cost) {
	// dest and src is 1 hop (in rip entry)
	pthread_mutex_lock(&rtlock);
	int i = 0;
	bool result = false;

	list<Entry *>::iterator pt = rt.begin();
	for (; pt != rt.end(); pt++) {
		if ((*pt)->destIP == dest) {
			if ((*pt)->nextIP == src) {
				if ((*pt)->cost != cost + 1 && cost != 16) {
					(*pt)->cost = cost + 1;
					result = true;
				}
				(*pt)->timeStamp = 0;
			}
			else if ((*pt)->cost > cost + 1) {
				(*pt)->cost = cost + 1;
				(*pt)->nextIP = src;
				(*pt)->timeStamp = 0;
				result = true;
			}
			break; // destination is unique
		}
		else if ((*pt)->destIP == src && (*pt)->nextIP == src) {
			(*pt)->timeStamp = 0;
			// no need to change result
			break;
		}
	}
	if (pt == rt.end()) {
		result = true;
		dest = (isDest(dest))? src : dest;
		Entry *entry = new Entry(dest, cost, src);
		entry->timeStamp = 0;
		rt.push_back(entry);
	}
	pthread_mutex_unlock(&rtlock);
	return result;
}

bool Router::sendRip() {
	pthread_mutex_lock(&rtlock);

	int num = rt.size();
	size_t pktlen = sizeof(ip_t) + sizeof(rip_t) + num * sizeof(rip_entry_t);
	char *buf = (char *)malloc(pktlen * sizeof(char));
	ip_t *iph = (ip_t *)buf;
	rip_t *riph = (rip_t *)(buf + sizeof(ip_t));
	rip_entry_t *ent = (rip_entry_t *)(buf + sizeof(ip_t) + sizeof(rip_t));

	list<Entry *>::iterator pt = rt.begin();

	riph->cmd = 0;
	riph->num = rt.size();
	for (; pt != rt.end(); pt++) {
		ent->cost = (*pt)->cost;
		ent->addr = (*pt)->destIP;
		ent += 1;
	}

	int itnum = it.size();
	int i = 0;
	iph->protocal = 200;
	for (; i < itnum; i++) {
		iph->src = it[i]->localIP;
		iph->dest = it[i]->nextIP;
		node->sendp(i, buf, pktlen);
	}

	free(buf);
	pthread_mutex_unlock(&rtlock);

	return true;
}

bool Router::recvRip(char *buf) {
	ip_t *iph = (ip_t *)buf;
	rip_t *riph = (rip_t *)(buf + sizeof(ip_t));
	rip_entry_t *entry = (rip_entry_t *)((char *)(riph) + sizeof(rip_t));
	int num = riph->num;
	int i = 0;
	in_addr_t src = iph->src;
	// in_addr_t nextHop;
	// int cost;
	bool result = true;
	bool flag = false;
	for (; i < num; i++) {
		if (rtUpdate(entry->addr, src, entry->cost)) {
			// send rip update to others
			flag = true;
		}
		entry += 1;
	}
	if (flag) {
		if (!sendRip()) {
			result = false; 
		}
	}
	return result;
}

void *routerRecv(void *a) {
	printf("handler function\n");
	char *arg = (char *)a;

	Router *router = NULL;
	memcpy(&router, arg, sizeof(void *));
	
	Sockaddr_in sin;
	socklen_t addrlen = sizeof(Sockaddr_in);
	memcpy(&sin, arg + sizeof(void *), addrlen);

	char *buf = (char *)malloc(MAX_IP_LEN);
	memcpy(buf, arg + sizeof(void *), MAX_IP_LEN);
	
	free(arg);

	ip_t *iph = (ip_t *)buf;
	char *ax = inet_ntoa(sin.sin_addr);
	in_addr_t ay = inet_addr(ax);
	if (router->findIt(ay) == -1) {
		printf("receive packet from a down interface\n");
	}
	else if (router->isDest(iph->dest)) {
		if (iph->protocal == 0) {
			printf("recv: %s\n", buf + sizeof(ip_t));
		}
		else {
			if (!router->recvRip(buf)) {
				printf("cannot rip\n");
			}
		}
	}
	else {
		assert(iph->protocal == 0);
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

bool Router::wrapSend(in_addr_t destIP, const char *msg, bool flag) {
	int id = findIt(destIP);
	if (id < 0) {
		printf("cannot routing\n");
		return true;
	}
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

	bool result = node->sendp(id, packet, len);
	free(packet);
	return result;
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
	
	pthread_mutex_unlock(&rtlock);
	
	if (pt == rt.end() || (*pt)->cost > 15) {
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


	
	while (!que.empty()) {
		msg = que.front();
		que.pop();
		if (!wrapSend(dest, msg.c_str(), true)) {
			return false;
		}
	}
	return true;
}

bool Router::send(string d, string longMsg) {
	in_addr_t dest = inet_addr(d.c_str());
	return send(dest, longMsg);
}