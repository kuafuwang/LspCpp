#pragma once

#include "LibLsp/JsonRpc/serializer.h"



#include <string>
#include <vector>


struct lsPosition {
	lsPosition();
	lsPosition(int line, int character);

	bool operator==(const lsPosition& other) const;
	bool operator<(const lsPosition& other) const;

	std::string ToString() const;

	// Note: these are 0-based.
	int line = 0;
	int character = 0;
	static const lsPosition kZeroPosition;

	void swap(lsPosition& arg) noexcept
	{
		std::swap(line, arg.line);
		std::swap(character, arg.character);
	}
};
MAKE_REFLECT_STRUCT(lsPosition, line, character);