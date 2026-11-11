#pragma once
#include <span>
#include <jde/calendar/Schedule.h>

//The rows of docs/oee/spec.md, as the engine writes them and the formulas read them.  No database, no QL and no historian here.
namespace Jde::Opc::Oee{
	using Calendar::Span;

	enum class ERole : uint8{ State, Total, Rate, Good, Reject, Reason };
	enum class EState : uint8{ Running, Stopped, Unknown };
	enum class ECategory : uint8{ Unplanned, Planned, Excluded };
	enum class ELevel : uint8{ Site, Area, Line, Cell };//ISA-95's below the enterprise, highest first.
	α ToString( ERole role )ι->sv;
	α ToString( EState state )ι->sv;
	α ToString( ECategory category )ι->sv;
	α ToString( ELevel level )ι->sv;
	α ToRole( sv name )ι->optional<ERole>;//as ToString spells it, the columns' text.
	α ToCategory( sv name )ι->optional<ECategory>;
	α ToLevel( sv name )ι->optional<ELevel>;

	//A State value that means running:  a number, a Boolean as 1 or 0, or text.
	using StateValue = std::variant<double,string>;
	struct Signal final{//an oee_machine_signals row.
		ERole Role{};
		uint32 Node{};//the member's hist_group_nodes id, which is its node_index in the group's records.
		vector<StateValue> Running{ StateValue{1.0} };//State's:  [true] by default.
		Duration RatePeriod{ minutes{1} };//Rate's time unit.
	};
	α Find( std::span<const Signal> signals, ERole role )ι->const Signal*;
	α HasCount( std::span<const Signal> signals )ι->bool;//a Total, Rate, Good or Reject:  with a cycle, performance is measured.
	α MeasuresQuality( std::span<const Signal> signals )ι->bool;//two of Total or Rate, Good and Reject, so good and total are both counted.

	struct Interval final{//an oee_intervals row.
		TimePoint Start;
		Duration Width{};
		Duration Run{}, Stop{}, Unknown{};//summing to Width.
		double Total{}, Good{}, Reject{};
		α End()Ι->TimePoint{ return Start+Width; }
		bool operator==( const Interval& )const = default;
	};
	struct Stop final{//an oee_stops row:  a stop, or with Unknown a span of unknown time.
		TimePoint Start;
		optional<TimePoint> End;//none while it is open.
		optional<int64_t> Code;//the Reason signal's.
		bool Unknown{};
		uint32 Id{};//the row's;  0 from the engine.
		bool operator==( const Stop& )const = default;
	};
	struct Reason final{//an oee_reasons row.
		uint32 Id{};
		string Name;
		ECategory Category{};
		optional<int64_t> Code;//what the Reason signal reports for it.
	};
	struct Coding final{//an oee_codings row:  an operator's reason over [Start, End) of a machine's time.
		uint32 Id{};
		TimePoint Start;
		TimePoint End;
		uint32 ReasonId{};
		string Comment;
		string UserName;
	};
	struct Machine final{//what a figure takes of an oee_machines row and its signals.
		uint32 Id{};
		string Name;
		optional<Duration> IdealCycle;//per unit.
		Duration SmallStop{};
		bool Counts{};//HasCount.
		bool Quality{};//MeasuresQuality.
	};
}