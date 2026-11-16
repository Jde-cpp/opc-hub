#pragma once
#include <random>
#include <jde/fwk/chrono.h>

namespace Jde::Opc::Emulator{
	struct Device;
	//`key`'s value, none when it is absent;  present as anything else it throws, naming `owner` - a typo has to fail like
	//one, not read as the default (`cycle: "2s"`).  T:  Duration (ISO 8601), bool, string or a number.
	Ŧ FindKey( const jobject& o, sv key, sv owner, SRCE )ε->optional<T>;

	//A window that comes round on its owner's clock, which starts with the emulator:  a device's stop or slow period, a tag's
	//quality window.  It holds the window it is in or the next, and moves on to the one after at its End (Advance).  Four
	//shapes.  {start, for}:  once, from `start` (0) for `for`.  {start, every, for}:  from `start` - left out, the first
	//`every`, not 0 - and again every `every`, for `for`, the shorter.  {mean, min, max}:  after a gap from the last one's end,
	//the first's from 0, drawn about `mean`, exponential, for a length drawn from [min, max], both from the engine Advance is
	//given.  {at, for}:  from a time of day in the owner's zone for `for`, under 23 hours, the shortest day a clock change makes,
	//the one under way at the start included;  a time the clocks skip is the instant they jump to, one they repeat its first.
	//A key of no shape's but the owner's `own` (a stop's `code`, a slow period's `factor`, a quality window's `status` and the
	//rest of its own) is refused.
	struct Recurrence{
		enum class EShape : uint8{ Once, Every, Random, At };
		Recurrence( const jobject& o, sv owner, std::initializer_list<sv> own, SRCE )ε;
		α IsActive( Duration t )Ι->bool{ return t>=Begin && t<End; }
		α NextEdge( Duration t )Ι->Duration{ return t<Begin ? Begin : End; }
		//To the window after this one, once the clock is at its End - a one-shot's to none.  The constructor places an {every} or
		//one-shot window, which the clock alone fixes;  a random or `at` one is placed by its first Advance, from Begin=End=0, as
		//the ones after it.  `device` is the clock's, its Start and Zone an `at` window's;  `engine` is what a random one draws
		//from - the device's for its stops and slow periods, a tag's own for its quality windows (TagQuality).
		α Advance( const Device& device, std::mt19937_64& engine )ι->void;
		//An `at` window keeps to the wall clock:  moved by `drift`, what Device::Anchor moved Start by, and on to the next once
		//the clock is past it.  The other shapes keep the device clock.
		α Anchor( Duration drift, const Device& device )ι->void;
		α Shortest()Ι->Duration{ return Shape==EShape::Random ? Min : Length; }
		//The run between two windows:  the shortest an {every} or {at} entry has, a random entry's mean - its gaps are exponential;
		//none after a one-shot.
		α Gap()Ι->optional<Duration>{ return Shape==EShape::Once ? nullopt : optional{ Shape==EShape::Every ? Every-Length : Shape==EShape::At ? 23h-Length : Mean }; }
		EShape Shape;
		Duration Every{}, Mean{}, Min{}, Max{};
		Duration At{};//since the local midnight.
		Duration Length{};//`for`;  a random entry's, this window's draw.
		Duration Begin{}, End{};//this window's;  a one-shot's past its window are Duration::max().
	};

	Ŧ FindKey( const jobject& o, sv key, sv owner, SL sl )ε->optional<T>{
		const auto v = o.if_contains( key );
		if( !v )
			return {};
		optional<T> y;
		sv kind;
		if constexpr( std::is_same_v<T,Duration> ){
			kind = "an ISO 8601 duration, PT30S";
			if( v->is_string() ){
				if( auto d = Chrono::TryToDuration(v->get_string(), ELogLevel::Trace, sl); d )
					y = *d;
			}
		}
		else if constexpr( std::is_same_v<T,bool> ){
			kind = "true or false";
			if( v->is_bool() )
				y = v->get_bool();
		}
		else if constexpr( std::is_same_v<T,string> ){
			kind = "a string";
			if( v->is_string() )
				y = string{ v->get_string() };
		}
		else{
			kind = "a number";
			if( auto n = v->try_to_number<T>(); n )
				y = *n;
		}
		THROW_IFSL( !y, "{}: `{}` is {} - it takes {}.", owner, key, serialize(*v), kind );
		return y;
	}
}