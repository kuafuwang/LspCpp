#pragma once

#include "LibLsp/JsonRpc/serializer.h"



#include <string>
#include <vector>
#include "lsPosition.h"

struct lsRange {
	lsRange();
	lsRange(lsPosition start, lsPosition end);

	bool operator==(const lsRange& other) const;
	bool operator<(const lsRange& other) const;
	
	lsPosition start;
	lsPosition end;
	std::string ToString()const;
	MAKE_SWAP_METHOD(lsRange, start, end)
};

MAKE_REFLECT_STRUCT(lsRange, start, end)