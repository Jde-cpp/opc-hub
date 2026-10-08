#pragma once
#include <map>
#include "DayFiles.h"

namespace Jde::Opc::Hist{
	//A day's modifications, as its modifications file holds them (spec *Edits*):  by target time, each time's in the
	//order the edits were made, which is the file's.
	struct DayMods final{
		//Reads the served file through:  throws when it can't be read through.
		Ω Read( const GroupFiles::Served& served, SL sl )ε->DayMods;
		α Empty()Ι->bool{ return ByTime.empty(); }
		//Each as a record, in target-time order, then the order the edits were made:  what a modified read serves.
		α Records()Ι->vector<Proto::HistoryRecord>;
		std::map<Ticks,vector<Proto::Modification>> ByTime;
	};
	//A day's records in primary-time order with its modifications applied, so a reader sees the corrected series:  the
	//day's merge, the records at each time taken together, and the modifications at that time applied to them in the
	//order they were made.  An INSERT appends its value after the records at the time, a REPLACE or UPDATE puts its value in
	//place of the last record of its node there that is its original, or appends it when it has none, and a DELETE removes
	//that record; one whose original isn't there changes nothing.  A record's place in its file is kept only where no
	//modification targets its time:  a page resumed at its offset would apply them to part of the time's records.
	struct Applied final : noncopyable{
		Applied( sp<ReadHandle> file, vector<Run> runs, vector<Proto::HistoryRecord> late, DayMods mods, SL sl )ι;
		//False at the end.  Throws when a run can't be read through.
		α Next( Proto::HistoryRecord& r, optional<Merge::Position>& where )ε->bool;
	private:
		struct Placed{ Proto::HistoryRecord Record; optional<Merge::Position> Where; };
		α Advance()ε->void;//the merge's next into _ahead.
		Merge _merge;
		DayMods _mods;
		std::map<Ticks,vector<Proto::Modification>>::const_iterator _nextMod;
		std::deque<Placed> _queue;//the records at one time, applied.
		optional<Placed> _ahead;
	};
	//Applies mods, one time's, to the records there, as above.
	α Apply( vector<Proto::HistoryRecord>& at, vector<optional<Merge::Position>>* where, const vector<Proto::Modification>& mods )ι->void;
}