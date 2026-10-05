#pragma once
#include <fstream>
#include "Reader.h"

namespace Jde::Opc::Hist{
	//A day's records in primary-time order:  the runs of its file, each in order, merged with one more in memory, a
	//flush's records for the day.  Ties go to the earlier run, and to the file's before the flush's, so the merge is as
	//stable as a sort.  A run is opened only when the merge reaches its first time, so memory follows how many runs
	//overlap, not how many there are, and one handle serves them all.  The FileStart and the checkpoints are left out.
	struct Merge final : noncopyable{
		//late is in order too.  Throws when file, which runs are of, can't be opened.
		Merge( fs::path file, vector<Run> runs, vector<Proto::HistoryRecord> late, SRCE )ε;
		~Merge();
		//False at the end.  Throws when a run can't be read through.
		α Next( Proto::HistoryRecord& r )ε->bool;
		α Unreadable()Ι->bool{ return _unreadable; }//a run its scan kept no longer reads as it did:  the file changed.
	private:
		struct Source;
		α First( uint index )Ι->Ticks;
		α Advance( Source& source )ε->bool;//its next record:  false at its end.

		const fs::path _path;
		const vector<Run> _runs;
		vector<Proto::HistoryRecord> _late;
		uint _lateNext{};
		std::ifstream _file;
		vector<uint> _order;//each run's index, and late's after them, by first time.
		uint _next{};//into _order:  the first not yet open.
		vector<up<Source>> _open;//a heap, the earliest record first.
		bool _unreadable{};
		SL _sl;
	};
}
