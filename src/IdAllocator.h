#pragma once
#ifndef INC_IDALLOCATOR_H
#define INC_IDALLOCATOR_H

// ID helpers for handle maps keyed by unsigned int. ID 0 is never returned
// because the Pawn API uses it as "invalid".

// Smallest free ID starting at 1. Deterministic: the first connection is
// always 1 (the default connectionHandle of the R39 natives) and closing and
// reopening a connection gives the same ID back.
template<class Map>
unsigned int GetLowestFreeId(const Map &map)
{
	unsigned int id = 1;
	while (map.find(id) != map.end())
		++id;
	return id;
}

// Next free ID after the previously returned one, wrapping around and skipping
// IDs still in use. Amortized O(1), and a freed ID is not handed out again
// right away, so a stale ID kept by a script does not silently refer to a new
// object.
template<class Map>
unsigned int GetNextFreeId(const Map &map, unsigned int &next)
{
	// Cell values are signed in Pawn; stay in the positive range.
	static const unsigned int max_id = 0x7FFFFFFF;
	for (;;)
	{
		if (next == 0 || next > max_id)
			next = 1;
		const unsigned int id = next++;
		if (map.find(id) == map.end())
			return id;
	}
}

#endif // INC_IDALLOCATOR_H
