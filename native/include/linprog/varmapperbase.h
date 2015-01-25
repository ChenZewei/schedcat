#ifndef VARMAPPERBASE_H
#define VARMAPPERBASE_H

#include <stdint.h>

#include "stl-hashmap.h"

class VarMapperBase {

private:
	hashmap<uint64_t, unsigned int> map;
	unsigned int next_var;
	bool sealed;

protected:
	void insert(uint64_t key)
	{
		assert(next_var < UINT_MAX);
		assert(!sealed);

		unsigned int idx = next_var++;
		map[key] = idx;
	}

	bool exists(uint64_t key) const
	{
		return map.count(key) > 0;
	}

	unsigned int get(uint64_t key)
	{
		return map[key];
	}

	unsigned int var_for_key(uint64_t key)
	{
		if (!exists(key))
			insert(key);
		return get(key);
	}

public:

	VarMapperBase(unsigned int start_var = 0)
		: next_var(start_var), sealed(false)
	{}


	// stop new IDs from being generated
	void seal()
	{
		sealed = true;
	}

	unsigned int get_num_vars() const
	{
		return map.size();
	}

	unsigned int get_next_var() const
	{
		return next_var;
	}

};


#endif