#pragma once
#include "types.h"

//The rule checks of docs/oee/spec.md *Machines*, *Intervals* and *Lines* that need only the rows as a mutation would
//leave them.  Each throws a Jde::Exception with EHttpStatus::BadRequest whose message is what the client sees;  the
//mutation await adds the checks that need the database, the historian or the calendar client.
namespace Jde::Opc::Oee{
	constexpr std::array<uint8_t,12> Widths{ 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60 };//minutes:  the widths that divide an hour.
	α ValidateWidth( Duration width, SRCE )ε->void;
	//A signal's `running`:  the column's json text, or its list, of Booleans, numbers and strings;  null is [true].
	α ParseRunning( const jvalue& running, SRCE )ε->vector<StateValue>;
	//A role once;  a State or a count, Total or Rate, and not both counts;  a State's running list with a value in it;  a
	//Rate's period above zero.
	α ValidateSignals( std::span<const Signal> signals, SRCE )ε->void;
	//The signals, the width, an ideal cycle and an idle timeout above zero when set, and an idle timeout on a machine with
	//no State signal, whose counts are its state.
	α ValidateMachine( std::span<const Signal> signals, Duration width, optional<Duration> idealCycle, optional<Duration> idleTimeout, SRCE )ε->void;

	namespace Lines{
		struct Line final{ uint32 Id{}; string Name; ELevel Level{}; };//an oee_lines row.
		struct Member final{//an oee_line_members row:  a machine or a line, one of the two.
			uint32 LineId{};
			optional<uint32> MachineId;
			optional<uint32> MemberLineId;
		};
		//The lines and their memberships, in member order, as the mutation would leave them.
		struct Graph final{
			vector<Line> Lines;
			vector<Member> Members;
			flat_map<uint32,string> Machines;//names, for the messages:  an id without one is named by its number.
		};
		//Each member one machine or one line that exists, no line reaching itself, no line reaching a machine by two paths,
		//and no member line at a level above its parent's.
		α Validate( const Graph& graph, SRCE )ε->void;
		//The machines a line reaches, each once, depth first in member order:  the order of its rows on the pages.  Throws
		//for a line the graph does not hold, or one that reaches itself.
		α Reach( const Graph& graph, uint32 lineId, SRCE )ε->vector<uint32>;
	}
}