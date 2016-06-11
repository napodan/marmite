/*
 * Statically sized hash table implementation
 * (C) 2012  Sasha Levin <levinsasha928@gmail.com>
 */

#ifndef _LINUX_HASHTABLE_H
#define _LINUX_HASHTABLE_H


#define DECLARE_HASHTABLE(name, bits)                                   	\
	struct hlist_head name[1 << (bits)]

#endif
