#ifndef PROTOCAL_H
#define PROTOCAL_H

struct ip_t {
	#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned int hl:4;		/* header length */
    unsigned int version:4;		/* version */
	#elif __BYTE_ORDER == __BIG_ENDIAN
    unsigned int version:4;		/* version */
    unsigned int hl:4;		/* header length */
	#else
	#error "Byte ordering not specified " 
	#endif
	uint8_t tos;
	uint16_t length;
	uint16_t iden;
	uint16_t offset;
	uint8_t ttl;
	uint8_t protocal;
	uint16_t sum;
	uint32_t src;
	uint32_t dest;
	uint32_t padding;
};

struct rip_t {
	uint16_t command;
	uint16_t num;
};

struct rip_entry_t {
	uint32_t cost;
	uint32_t address;
};

#endif