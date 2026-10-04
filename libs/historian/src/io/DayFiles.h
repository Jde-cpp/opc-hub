#pragma once
#include <functional>
#include <map>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include "Flushed.h"
#include "Reader.h"

namespace Jde::Opc::Hist{
	using Day = std::chrono::year_month_day;
	//The day a record with primary time t is filed under.  A time outside 1601 through 9999, which UA has no date for,
	//takes the nearest day inside.
	α DayOf( Ticks t, const std::chrono::time_zone& tz )ι->Day;
	//When day starts in tz, which its file's FileStart and preamble carry.
	α StartOf( Day day, const std::chrono::time_zone& tz )ι->Ticks;
	//<yyyy>/<m>/<d>, as the log's archive names a day.
	α DayDirectory( Day day )ι->fs::path;

	//What a group's files say of it at start:  each member's index, the index an Issued group issues next, and its last
	//flush, which is its break when the process stopped or crashed.
	struct Restored final{
		flat_map<ExNodeId,NodeIndex> Members;
		NodeIndex NextIndex{ 1 };
		optional<TimePoint> Flushed;
	};

	//What a file asks of its group's membership as it is written.
	struct Membership final{
		std::function<vector<std::pair<NodeIndex,ExNodeId>>()> Current;//every member, in index order:  a new file's preamble.
		std::function<optional<ExNodeId>( NodeIndex )> Find;//a member's, or that of one that left.
		NodeIndex NextIndex{};//what an Issued group issues next, which a new file's FileStart carries forward; else 0.
	};

	//A day's file as the process knows it:  from its first-open scan, and from its own appends since.
	struct DayFile final{
		fs::path Path;
		uint Size{};//through its last checkpoint, where the next append goes.  At 0 that append opens with the preamble.
		Ticks Chain{};//where its delta chain stands.
		vector<Run> Runs;//each append that holds a record with a time, as the first-open scan would rebuild them.
		absl::flat_hash_set<NodeIndex> Mapped;//the indexes its NodeAdded records name.
		optional<Scanned> Unopened;//its scan, until the first append drops a torn tail by it.
		bool Named{};//this process made it, or has fsynced its name into its directory:  one that crashed may have made it and not.
		string Refused;//why the historian won't append to it; empty when it will.
		uint Discarded{};//the appends refused so far.
	};

	//One append to a day's file, a run, between GroupFiles::Prepare and Commit.
	struct Pending final{
		Day Date;
		fs::path Path;
		uint Offset{};//the file's bytes that stay, which the run follows.
		string Bytes;
		//What Commit takes:  whether the file was there, its size with the run, and what the run adds to it.
		bool Existed{};
		uint End{};
		Ticks Chain{};
		vector<Run> Runs;
		absl::flat_hash_set<NodeIndex> Mapped;
		vector<Proto::DataValue> Stored;//each node's newest in the run.
		//Cuts the file to Offset, writes the run after it and fsyncs.  A file this makes has its directories fsynced too.
		α Write( SRCE )ι->IO::WriteAwait;
	};

	//One group's files under hist.path.  Only its group's flush changes them, one flush at a time, under the group's
	//files lock.
	struct GroupFiles final{
		//Reads the group's .flushed file, and its newest day file for AtStart() and each node's last stored value.  Throws
		//when that file can't be read through.
		GroupFiles( fs::path root, string name, const std::chrono::time_zone& tz, Day today, SRCE )ε;
		α AtStart()Ι->const Restored&{ return _restored; }
		//One append to day's file, a run:  records, sorted by primary time with their times absolute, then a checkpoint.
		//A file that isn't there opens with its preamble.  A node the file holds a value of and doesn't map gets a preamble
		//record in the run.  None for a file the historian won't append to, said at Error the first time.  Throws when the
		//file can't take an append.  What it knows of the file changes only in Commit.
		α Prepare( Day day, vector<Proto::HistoryRecord>&& records, const Membership& members, SRCE )ε->optional<Pending>;
		//Once the run's Write has returned.  The first append to a file an earlier process made fsyncs its directories
		//first, and throws when it can't, with the run still to be written again.
		α Commit( Pending&& run, SRCE )ε->void;
		//The days after day that hold a file of the group, which a membership change on day is copied into.
		α LaterDays( Day day )Ι->vector<Day>;
		α Find( Day day )Ι->const DayFile*;//none until the process opens it.
		α LastFlush()ι->Flushed&{ return _flushed; }
	private:
		α Open( Day day, SL sl, const std::function<void( Proto::HistoryRecord& )>& restore={} )ε->DayFile&;
		α Restore( Proto::HistoryRecord& r, flat_map<NodeIndex,ExNodeId>& members )ι->void;
		α Newer( Proto::DataValue&& stored )ι->void;
		α Added( NodeIndex index, const ExNodeId& node, Ticks start )Ι->Proto::HistoryRecord;

		const fs::path _root;
		const string _name;
		const std::chrono::time_zone& _tz;
		Flushed _flushed;
		Restored _restored;
		//#229's rewrite drops a day's entry once it is an archive.
		std::map<Day,DayFile> _files;
		flat_set<Day> _days;//each day known to hold a file:  from the newest back to the first that isn't after the start's day, and each made since.
		//Each node's newest stored value, a heartbeat or a marker included, which a new file's preamble takes as its start
		//value when it is from before the file's day.
		absl::flat_hash_map<NodeIndex,Proto::DataValue> _last;
	};
}
