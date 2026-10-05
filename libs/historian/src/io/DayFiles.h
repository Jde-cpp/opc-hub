#pragma once
#include <functional>
#include <map>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include "Flushed.h"
#include "Merge.h"

namespace Jde::Opc::Hist{
	//The day a record with primary time t is filed under.  A time outside 1601 through 9999, which UA has no date for,
	//takes the nearest day inside.
	α DayOf( Ticks t, const std::chrono::time_zone& tz )ι->Day;
	//Whether a day holds time t, 1601 through 9999:  DateTime's MaxValue, which a server may send for one it hasn't, is
	//past every day.
	α Fileable( Ticks t )ι->bool;
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
		struct Node final{ ExNodeId Id; bool Left; };
		std::function<optional<Node>( NodeIndex )> Find;//a member's, or that of one that left.
		NodeIndex NextIndex{};//what an Issued group issues next, which a new file's FileStart carries forward; else 0.
	};

	//A file's size and last write, which tell that something changed it.
	struct Stamp final{
		uint Size;
		fs::file_time_type Written;
		α operator==( const Stamp& )Ι->bool = default;
	};

	//A day's file as the process knows it:  from its first-open scan, and from its own appends since.  A live file's, or
	//one the historian leaves alone:  an archive's only while its day is merged into, so each merge needn't scan it.
	struct DayFile final{
		fs::path Path;
		uint32_t Generation{};//past 0, an archive, with no runs:  Size is all of it.
		uint Size{};//through its last checkpoint, where the next append goes.  At 0 that append opens with the preamble.
		Ticks Chain{};//where its delta chain stands.
		vector<Run> Runs;//each append that holds a record with a time, as the first-open scan would rebuild them.
		absl::flat_hash_set<NodeIndex> Mapped;//the indexes its NodeAdded records name.
		optional<Scanned> Unopened;//its scan, until the first append drops a torn tail by it.
		bool Named{};//this process made it, or has fsynced its name into its directory:  one that crashed may have made it and not.
		string Refused;//why the historian won't write to it; empty when it will.
		//As its first-open scan began, or its rewrite left it:  a refused file or an archive changed since, repaired perhaps,
		//is scanned again.
		optional<Stamp> Scanned;
		uint Discarded{};//the appends refused so far.
		//The append Prepare handed out and Commit hasn't taken, which may have failed part-way:  the Size it went at, and the
		//farthest it could have written.  The bytes past that Size, up to End, are its own, which the next append cuts.
		struct Append{ uint Offset; uint End; };
		optional<Append> Outstanding;
	};

	//One append to a day's file, a run, between GroupFiles::Prepare and Commit.
	struct Pending final{
		Day Date;
		fs::path Path;
		uint Offset{};//the file's bytes that stay, which the run follows.
		string Bytes;
		//What Commit takes:  whether the file was there, its size with the run, and what the run adds to it.
		bool Existed{};
		bool Restart{};//the file was removed or purged since, so what was known of it goes.
		uint End{};
		Ticks Chain{};
		vector<Run> Runs;
		absl::flat_hash_set<NodeIndex> Mapped;
		vector<Proto::DataValue> Stored;//each node's newest in the run.
		//Cuts the file to Offset, writes the run after it and fsyncs.  A file this makes has its directories fsynced too.
		α Write( SRCE )ι->IO::WriteAwait;
	};

	//A day's file written whole as its archive, between GroupFiles::Prepare and Commit:  the runs of the live file it was,
	//or the archive it already is, merged in primary-time order with a flush's records for the day.  The FileStart's
	//generation is one past the file's, the preamble comes first, with a corrected start value folded into its member's
	//first preamble record, and no checkpoint is carried over.  It goes to a temp file beside the day's in parts, so
	//neither the day nor the file is ever held whole, and Commit renames that over the day's.
	struct Rewrite final{
		struct State;
		Rewrite( Day date, fs::path path, fs::path temp, vector<Proto::DataValue> stored, up<State> state )ι;
		Rewrite( Rewrite&& )ι;
		α operator=( Rewrite&& )ι->Rewrite&;
		~Rewrite();
		Day Date;
		fs::path Path;
		fs::path Temp;
		vector<Proto::DataValue> Stored;//each node's newest among the flush's.
		//Makes the temp file's next part:  false once Write has written them all.  Throws when the file it merges can't be
		//read through.
		α Next()ε->bool;
		//Writes the part.  The first makes the temp file, or cuts one a crash left, and the last fsyncs it.
		α Write( SRCE )ι->IO::WriteAwait;
		α Unreadable()Ι->bool;//the file no longer reads as its scan did.
		α Archive()ι->DayFile;//what is known of the archive once Commit has renamed it, which no scan then needs to tell.
	private:
		up<State> _state;
	};

	//How a flush writes a day's records:  not at all, to a file the historian leaves alone or when there are none to
	//write; as a run on the end of its live file; or by rewriting its archive whole.
	using DayWrite = variant<std::monostate,Pending,Rewrite>;

	//One group's files under hist.path.  Only its group's flush changes them, one flush at a time, under the group's
	//files lock.
	struct GroupFiles final{
		//Reads the group's .flushed file, its newest day file for TakeRestored(), and every day file down to the first that isn't
		//after the present, today or its last flush's day when a clock set back makes that later, for each node's last
		//stored value.  Throws when one of them can't be read through.  From there the walk goes on down to the day
		//.flushed names, for the live files a midnight left behind (Due), and removes each temp file it passes, which a
		//rewrite that a crash cut short left.  A file there it can't tell about is said, and taken as live for its rewrite.
		GroupFiles( fs::path root, string name, const std::chrono::time_zone& tz, Duration delay, Day today, SRCE )ε;
		α TakeRestored()ι->Restored{ return move( _restored ); }//once, for the group's start:  nothing keeps it after.
		//A node's newest stored record in the files, a heartbeat or a marker included:  none when they hold nothing of it.
		α Newest( NodeIndex index )Ι->const Proto::DataValue*;
		//How a flush at now writes records to day's file:  sorted by primary time, with their times absolute.
		//
		//A live file takes them as a run on its end, closed by a checkpoint.  A file that isn't there opens with its
		//preamble.  A node the file holds a value of and doesn't map gets a preamble record in the run, and one that has
		//left the group a NodeRemoved after it, so the file's membership stays the group's.
		//
		//An archive is rewritten with them merged in, and so is the file of a day whose midnight rewrite is due:  `delay`
		//after the day's end, or at once for one the start found.  That one takes no records to become its archive, and a
		//file made for such a day is an archive from the start.
		//
		//Nothing for a file the historian leaves alone, said at Error the first time it drops records, until it is
		//removed, or changed, which scans it again.  Throws when the file can't be written, or holds bytes other than
		//what the historian wrote and an outstanding append left, dropping what it knows of the file so the next write
		//scans it again.  Otherwise what it knows of the file changes only in Commit, but for a torn tail it cuts and the
		//append it hands out.
		α Prepare( Day day, vector<Proto::HistoryRecord>&& records, const Membership& members, TimePoint now, SRCE )ε->DayWrite;
		//Once the run's Write has returned.  The process's first append to a file fsyncs its directories
		//first, and throws when it can't, with the run still to be written again.
		α Commit( Pending&& run, SRCE )ε->void;
		//Once the rewrite's last Write has returned:  renames its temp file over the day's, and fsyncs the directory.
		//Throws when the rename fails, with the day's file as it was.  When only the fsync fails the archive is in place,
		//its records with it, and SyncRenamed tries the fsync again.
		α Commit( Rewrite&& rewrite, TimePoint now, SRCE )ε->void;
		//fsyncs the directory of each archive whose Commit couldn't, so a power loss can't take back its rename:  false
		//while one still can't, which .flushed waits on, since it would claim the archive's records.
		α SyncRenamed( SRCE )ι->bool;
		//A rewrite that failed:  its temp file goes, and what is known of a file that no longer reads as it was scanned,
		//so the next write scans it again.
		α Abandon( const Rewrite& rewrite )ι->void;
		//The days whose file a flush at now rewrites as its archive though it has no records for them:  each live file
		//the process knows whose day ended `delay` ago, and each the start found.
		α Due( TimePoint now )ι->vector<Day>;
		α Recovers()Ι->bool{ return !_recover.empty(); }//the start found live files that a midnight left behind.
		//A removed group's:  each live file is due at once, and each write after is a rewrite, so none is left live.
		α Retire()ι->void;
		α Archived()Ι->bool{ return _unarchived.empty(); }//no file it knows of is live.
		//The day a start after a flush at flushed walks down to for live files:  the day before flushed's, whose midnight
		//rewrite may yet be cut short, or an older one whose file is still live, its rewrite failing.
		α RecoverFrom( TimePoint flushed )Ι->Day;
		//Whether day's file was rewritten less than `delay` before now:  the clock's flushes then hold its records, so an
		//archive is rewritten at most once per `delay` however often late records arrive.
		α Deferred( Day day, TimePoint now )Ι->bool;
		//Whether day's file opens to append, made with its directories when it isn't there, as a write's open does:  a
		//failing day's cheap retry, so a path that still can't take one costs no conversion of its records.  One that
		//opens may still fail its write, as a full disk does.
		α Openable( Day day )Ι->bool;
		//The days after day, and after the present, that still hold a file of the group, which a membership change on day is
		//copied into:  one purged since is forgotten.  A change a clock set back stamped before the group's files goes only
		//to those after its last flush.
		α LaterDays( Day day )ι->vector<Day>;
		α Present( Day today )ι->void;//a flush's day by the host clock, which moves the present on, never back.
		α Find( Day day )Ι->const DayFile*;//none until the process opens it.
		α LastFlush()ι->Flushed&{ return _flushed; }
	private:
		α Open( Day day, SL sl, const std::function<void( Proto::HistoryRecord& )>& restore={} )ε->DayFile&;
		α Past( Day day, TimePoint now )Ι->bool;//whether day's midnight rewrite is due:  it ended `delay` ago.
		α File( Day day )Ι->fs::path;
		α Temp( Day day )Ι->fs::path;//what a rewrite of day's file writes, beside it.
		α Restore( Proto::HistoryRecord& r, flat_map<NodeIndex,ExNodeId>& members )ι->void;
		α Fold( Proto::HistoryRecord& r )ι->void;//a value, or a NodeAdded's start value, into _last.
		α Newer( Proto::DataValue&& stored )ι->void;
		α Added( NodeIndex index, const ExNodeId& node, Ticks start )Ι->Proto::HistoryRecord;

		const fs::path _root;
		const string _name;
		const std::chrono::time_zone& _tz;
		const Duration _delay;
		Flushed _flushed;
		Restored _restored;
		std::map<Day,DayFile> _files;//an archive's only while _rewritten holds its day:  one merged into later is scanned again.
		//Each day whose live file is due at once, not at its midnight, until it is rewritten:  each before today the start
		//found, and each of a removed group's.
		flat_set<Day> _recover;
		bool _retired{};
		flat_set<Day> _unarchived;//each day known to hold a live file, _recover's among them, until it is rewritten.
		//When each archive was last rewritten, for two `delay`s:  the first holds the clock's merges into it, and through the
		//second, when the next comes, its entry in _files stays.  One after now, which a clock set back since leaves, has
		//expired, so neither lasts longer than it should.
		flat_map<Day,TimePoint> _rewritten;
		flat_set<Day> _unsynced;//each day whose archive's rename its directory's fsync hasn't yet made durable.
		flat_set<Day> _days;//each day known to hold a file:  from the newest back to the first that isn't after the present at start, and each made since.
		Day _present;//the latest of the host clock's day and its last flush's:  a clock set back doesn't move it.
		//Each node's newest stored value, a heartbeat or a marker included, which a new file's preamble takes as its start
		//value when it is from before the file's day.
		absl::flat_hash_map<NodeIndex,Proto::DataValue> _last;
	};
}
