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
	//The width, and an ideal cycle and an idle timeout above zero when set:  all a machine with no signal yet is held to.
	α ValidateThresholds( Duration width, optional<Duration> idealCycle, optional<Duration> idleTimeout, SRCE )ε->void;
	//The signals, those thresholds, and an idle timeout on a machine with no State signal, whose counts are its state.
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
		//The lines a line reaches, each once, depth first in member order, itself left out.  Throws as Reach does.
		α ReachLines( const Graph& graph, uint32 lineId, SRCE )ε->vector<uint32>;
	}

	//The floor's rules (docs/demo/spec.md *The floor*):  a line's map, with a shape per machine and line it reaches and the fixtures
	//around them, in metres from the floor's top-left.
	namespace Floors{
		enum class EKind : uint8{ Machine, Line, Zone, Wall, Aisle, Label };
		α ToString( EKind kind )ι->sv;
		α ToKind( sv name )ι->optional<EKind>;//as ToString spells it, the column's text.
		struct Point final{ double X{}, Y{}; bool operator==( const Point& )const = default; };
		//`points`:  the column's json text, or its list, of [x, y] pairs in metres.
		α ParsePoints( const jvalue& points, SRCE )ε->vector<Point>;
		α ToJson( std::span<const Point> points )ι->string;//as the column holds them.
		struct Floor final{ uint32 Id{}; uint32 LineId{}; double Width{}, Height{}, Grid{ 0.5 }; };//an oee_floors row.
		//An oee_floor_shapes row as a mutation would leave it.  Kind is none for text ToKind doesn't know, and Points empty for
		//json ParsePoints refuses:  a row the QL never wrote.
		struct Shape final{
			uint32 Id{};
			uint32 FloorId{};
			optional<EKind> Kind;
			optional<uint32> MachineId;
			optional<uint32> MemberLineId;
			vector<Point> Points;
		};
		//Width, height and grid above zero.
		α ValidateFloor( const Floor& floor, SRCE )ε->void;
		//Every point of every shape on or inside the floor:  a floor's resize, against the shapes it holds.
		α ValidateInside( const Floor& floor, std::span<const Shape> shapes, SRCE )ε->void;
		//A shape against its floor, the floor's other shapes and the graph:  its kind one of the six;  a Machine's machine one the
		//floor's line reaches, a Line's line one it reaches other than itself, each once on the floor, and neither key on any other
		//kind;  Machine, Line and Zone closed polygons of three points or more, Wall and Aisle polylines of two or more, a Label
		//one;  and every point on or inside the floor.  `others`:  the floor's shapes but this one.
		α ValidateShape( const Lines::Graph& graph, const Floor& floor, const Shape& shape, std::span<const Shape> others, SRCE )ε->void;
	}
}