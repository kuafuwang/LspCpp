#pragma once

#include "LibLsp/JsonRpc/serializer.h"

struct lsRequestId {
	// The client can send the request id as an int or a string. We should output
	// the same format we received.
	enum Type { kNone, kInt, kString };
	Type type = kNone;

	int value = -1;

	bool has_value() const { return type != kNone; }
	void swap(lsRequestId& arg) noexcept
	{

		Type temp = arg.type;
		type = arg.type;
		arg.type = temp;

		int  temp_id = arg.value;
		value = arg.value;
		arg.value = temp_id;
	}
	void set(int v)
	{
		value = v;
		type = kInt;
	}
	
};
void Reflect(Reader& visitor, lsRequestId& value);
void Reflect(Writer& visitor, lsRequestId& value);

// Debug method to convert an id to a string.
std::string ToString(const lsRequestId& id);