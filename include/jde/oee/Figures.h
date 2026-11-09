#pragma once
#include "types.h"

//The formulas of docs/oee/spec.md *Figures*, *Intervals* and *Lines*, computed when they are read:  over a machine's rows,
//its stops, the codings and the reasons, under a calendar snapshot and the machine's thresholds as they are then.
namespace Jde::Opc::Oee{
	//A machine's rows for a read.  The figures are over the time Rows cover, their first's start to their last's end, so
	//time the engine has not reached is in none:  the open interval's tallies ride as a last row of the width it holds so far.
	struct History final{
		std::span<const Interval> Rows;//by Start.
		std::span<const Stop> Stops;//by Start:  those that overlap the range, the unknown spans among them, which only Timeline reads.
		std::span<const Coding> Codings;//by Start;  a machine's do not overlap.
		std::span<const Reason> Reasons;//the gateway's list, the deleted among them:  one keeps resolving.
	};
	enum class EFactors : uint8{ None=0, Availability=1, Performance=2, Quality=4 };
	constexpr α operator|( EFactors a, EFactors b )ι->EFactors{ return (EFactors)( (uint8)a|(uint8)b ); }
	constexpr α operator&( EFactors a, EFactors b )ι->bool{ return (uint8)a & (uint8)b; }

	//A machine's figures over a bucket, or a line's, its members' combined.
	struct Bucket final{
		TimePoint Start;
		TimePoint End;
		string Shift;//the calendar's label:  the occurrence's name for a shift bucket.
		//Within planned time.  Planned is the calendar's less Excluded and Unknown;  Run is Planned less Unplanned and
		//PlannedStop, so a small stop is run time that made nothing.
		Duration Planned{}, Run{}, Unplanned{}, PlannedStop{}, Small{}, Excluded{}, Unknown{};
		double Total{}, Good{}, Reject{};//counted within planned time.
		double Outside{};//the total counted outside it:  in a break, an Excluded stop or outside the shifts.
		Duration Ideal{};//the total times the ideal cycle;  a line's, over its machines with a cycle.
		optional<double> Availability, Performance, Quality, OEE, TEEP;
		EFactors Measured{};//the factors in OEE;  a line's, those every machine measured.
		bool Partial{};//unknown time falls in it, or its rows do not cover it.
		//What a line sums besides.
		Duration Whole{};//the time of the bucket the rows cover, which TEEP is over.
		Duration PacedRun{};//the run time of the machines with a count and a cycle:  what Ideal is over.
		Duration GoodIdeal{}, GradedIdeal{};//the good count and the total times the cycle, of the machines that measure quality and have one.
		uint32 Machines{ 1 };
		uint32 PerformanceCoverage{}, QualityCoverage{};//how many of Machines each factor is over.
	};
	//One bucket over [start, end), or one per `by`, cut by the schedule's Buckets.  A row that straddles a bucket's edge or
	//planned time's is split by time, its counts and its unknown time taken as even across it;  the stops are exact.
	//Without a schedule every minute is planned and the days begin at midnight UTC;  by shift then throws a BadRequest.
	α Figures( const Machine& machine, const History& history, const Calendar::Schedule* schedule, TimePoint start, TimePoint end, optional<Calendar::BucketBy> by={}, SRCE )ε->vector<Bucket>;

	//A loss row:  the time of stops, within planned time, under one reason.  A small stop is under Small whatever its
	//coding, bar Excluded, and time no coding or code names under Uncoded.
	enum class ELoss : uint8{ Reason, Uncoded, Small };
	struct Loss final{
		ELoss Kind{};
		optional<Reason> Cause;//Kind Reason's.
		Duration Time{};
		uint32 Stops{};//the stops with time in the row.
	};
	α Losses( const Machine& machine, const History& history, const Calendar::Schedule* schedule, TimePoint start, TimePoint end )ι->vector<Loss>;//longest first.

	//What the Gantt chart draws of a machine:  the planned band and its time cut at every change.
	struct Segment final{
		TimePoint Start;
		TimePoint End;
		EState State{};
		optional<uint32> StopId;
		optional<Reason> Cause;//a stop's piece's:  its coding's, else its code's.
		bool Small{};//the stop is shorter than the threshold, by its whole length.
		bool Uncoded{};//a stop's piece no coding or code names.
		optional<uint32> CodingId;
	};
	struct MachineTimeline final{
		vector<Calendar::Occurrence> Planned;//the schedule's occurrences of the range;  without one, the range as an occurrence with no shift.
		vector<Segment> Segments;//over the time Rows cover within the range.
	};
	α Timeline( const Machine& machine, const History& history, const Calendar::Schedule* schedule, TimePoint start, TimePoint end )ι->MachineTimeline;

	namespace Lines{
		//A line's bucket over [start, end):  its members' sums, a machine's bucket or a member line's own, each factor over the
		//machines that measure it and OEE the product of the three as computed.
		α Combine( TimePoint start, TimePoint end, string shift, std::span<const Bucket> members )ι->Bucket;
	}
}