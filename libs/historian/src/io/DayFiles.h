#pragma once
#include <functional>
#include <map>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <jde/fwk/io/durable.h>
#include <jde/fwk/io/records/Days.h>
#include <jde/fwk/io/records/FileState.h>
#include <jde/fwk/io/records/Flushed.h>
#include <jde/fwk/io/records/Rewrite.h>
#include "Records.h"

namespace Jde::Opc::Hist{
	using Day = IO::Records::Day;
	//The day a record with primary time t is filed under.  A time outside 1601 through 9999, which UA has no date for,
	//takes the nearest day inside.
	α DayOf( Ticks t, const std::chrono::time_zone& tz )ι->Day;
	//Whether a day holds time t, 1601 through 9999:  DateTime's MaxValue, which a server may send for one it hasn't, is
	//past every day.
	α Fileable( Ticks t )ι->bool;
	//When day starts in tz, which its file's FileStart and preamble carry.
	α StartOf( Day day, const std::chrono::time_zone& tz )ι->Ticks;
	//A NodeAdded with no writer:  at its day's start, a preamble record, and otherwise a membership change that came with
	//none.
	Ξ Bare( const Proto::NodeAdded& added )ι->bool{ return !added.has_identity_id() && added.user_name().empty(); }
	using IO::Records::DayDirectory;//<yyyy>/<m>/<d>, as the log's archive names a day.
	//Every day under root that holds the file of the group name, its value file or with mods its modifications file,
	//oldest first:  the directory walk, so a read asked for a range of days opens only those that are there.  A day whose
	//file it can't tell of, in a directory it can't search say, is among them, so a read that reaches it fails as it
	//opens it.
	α Days( const fs::path& root, sv name, bool mods=false, SRCE )ε->vector<Day>;

	//What a group's files say of it at start:  each member's index, the index an Issued group issues next, and its last
	//flush, which is its break when the process stopped or crashed.
	struct Restored final{
		flat_map<ExNodeId,NodeIndex> Members;
		flat_map<NodeIndex,uint32_t> DataTypes;//each member's NodeAdded data_type that wasn't 0.
		NodeIndex NextIndex{ 1 };
		optional<TimePoint> Flushed;
	};

	//A member a file lists at its day's start, by a Bare NodeAdded there:  its NodeId, and its start value, none for a
	//member with none.  The last preamble record of the member counts, a correction included.
	struct Listed final{ ExNodeId Node; optional<Proto::DataValue> Start; };
	//A file's map of its indexes (spec *Record format*):  every index the file holds a value of is listed, so a read
	//joins a node's indexes by it, and a file that lists none of a node's holds nothing of the node.
	using Preamble = absl::flat_hash_map<NodeIndex,Listed>;

	//What a file asks of its group's membership as it is written.
	struct Membership final{
		struct Node final{ ExNodeId Id; bool Left; const UA_DataType* DataType{}; };//none for one that left.
		std::function<vector<std::pair<NodeIndex,Node>>()> Current;//every member, in index order:  a new file's preamble.
		std::function<optional<Node>( NodeIndex )> Find;//a member's, or that of one that left.
		NodeIndex NextIndex{};//what an Issued group issues next, which a new file's FileStart carries forward; else 0.
		//The node's last record at or before through, as a read serves it, the edits applied:  the walk back, for a start
		//value the group's newest records can't give.  Called under the files lock.
		std::function<optional<Proto::DataValue>( NodeIndex, Ticks through )> Last;
	};

	using Stamp = IO::Records::Stamp;

	//A day's file as the process knows it:  the record layer's state of it - from its first-open scan, and from its own
	//appends since - and its map.  A live file's, or one the historian leaves alone:  an archive's only while its day is
	//merged into, so each merge needn't scan it.
	struct DayFile final : IO::Records::FileState{
		Hist::Preamble Preamble;//a value file's start values are what the corrections a late record or an edit reaches look at.
	};

	//One append to a day's file, a run, between GroupFiles::Prepare and Commit:  its value file's, or with Mods its
	//modifications file's.
	struct Pending final{
		Day Date;
		bool Mods{};
		fs::path Path;
		uint Offset{};//the file's bytes that stay, which the run follows.
		string Bytes;
		//What Commit takes:  whether the file was there, its size with the run, and what the run adds to it.
		bool Existed{};
		bool Restart{};//the file was removed or purged since, so what was known of it goes.
		uint End{};
		Ticks Chain{};
		vector<Run> Runs;
		vector<std::pair<NodeIndex,Listed>> Preamble;//each member's last preamble record in the run.
		vector<Proto::DataValue> Stored;//each node's newest in the run.
		//Cuts the file to Offset, writes the run after it and fsyncs.  A file this makes has its directories fsynced too.
		α Write( SRCE )ι->IO::WriteAwait;
	};

	//A day's file written whole as its archive, between GroupFiles::Prepare and Commit:  the record layer's rewrite - the
	//runs of the live file it was, or the archive it already is, merged in primary-time order with a flush's records for
	//the day, under a FileStart one generation on, in parts to a temp file beside the day's, with no checkpoint carried
	//over - with the historian's head:  an Issued group's next index, then the preamble, a corrected start value folded
	//into its member's first preamble record.  Commit renames the temp file over the day's.
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
		//Makes the temp file's next part:  Ready to Write, More when it wants bytes of the file it merges (Wants, Feed), Done
		//once Write has written the last.  Throws when the file can't be read through.
		α Next()ε->IO::Records::EPart;
		α Wants()Ι->optional<Window>;
		α Feed( string&& bytes )ι->void;
		α File()Ι->const sp<IO::OpenFile>&;
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

	//A day file as an Opening read it.
	struct Opened final{
		fs::path Path;
		optional<Stamp> Stamped;//none for a file that wasn't there.
		optional<Scanned> Scan;
		Hist::Preamble Preamble;
	};
	//A value file's first open for the flush, outside the files lock:  the record layer's scan, which the flush feeds a window
	//at a time through the file awaitables (Wants, Feed, Next), and the map it builds.  Nothing to read when the file isn't
	//there.  Throws when it is there and can't be opened or read through.
	struct Opening final : noncopyable{
		Opening( fs::path path, Ticks start, SRCE )ε;
		α Next()ε->ENext;//More:  Wants/Feed.  End:  Take.
		α Wants()Ι->optional<Window>;
		α Feed( string&& bytes )ι->void;
		α File()Ι->const sp<IO::OpenFile>&{ return _file; }
		α Take()ι->Opened{ return move( _opened ); }
	private:
		Opened _opened;
		sp<IO::OpenFile> _file;
		Decoder _decoder;
		optional<IO::Records::Scanner> _scanner;
	};

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
		//A node's newest stored record in the files, a heartbeat or a marker included, as the edits leave it:  none when they
		//hold nothing of it.
		α Newest( NodeIndex index )Ι->const Proto::DataValue*;
		//As an edit at or after the node's newest record leaves it:  Newest from here on, and the start value of the next
		//file made.  None when the files hold nothing of the node now.
		α Relast( NodeIndex index, optional<Proto::DataValue> newest )ι->void;
		//Each day the start read for the newest values that has a modifications file, or may have:  the newest of each node
		//its edits name may have moved, so the group reads those through the edits instead.
		α Edited()Ι->const vector<Day>&{ return _edited; }
		α Root()Ι->const fs::path&{ return _root; }
		α Name()Ι->const string&{ return _name; }
		//Whether day holds a value file:  one the process knows of with anything in it, or one on disk.
		α HasFile( Day day )Ι->bool;
		//The flush's first opens, outside the files lock:  of days, each whose value file the process hasn't opened, or holds as
		//refused or as an archive and that has changed since, with the file's path for an Opening.
		α ToScan( const vector<Day>& days )Ι->vector<std::pair<Day,fs::path>>;
		//What an Opening read, taken under the lock as Open would have:  nothing when the process opened the day meanwhile, or
		//the file changed since the open, which Prepare then opens itself.
		α Adopt( Day day, Opened&& opened )ι->void;
		//An Opening that failed:  the day's next Open throws it, once, rather than open the file again, as the one open did
		//before.  The flush after tries the file again.
		α Adopt( Day day, up<Exception> failed )ι->void;
		//How a flush at now writes records to day's file:  sorted by primary time, with their times absolute.
		//
		//A live file takes them as a run on its end, closed by a checkpoint.  A file that isn't there opens with its
		//preamble.  A node the file holds a value of and doesn't list gets a preamble record in the run, one that joined
		//mid-day included, so the file's map holds every index it has a value of, and one that has left the group a
		//NodeRemoved after it, so the file's membership stays the group's.
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
		//
		//A file that isn't there is made only for records, unless create:  an edit that targets a day with no value file
		//makes it, with its preamble alone, before its modifications file.
		α Prepare( Day day, vector<Proto::HistoryRecord>&& records, const Membership& members, TimePoint now, SRCE, bool create=false )ε->DayWrite;
		//An edit's Modification records for day, in target-time order, as a run on the end of its modifications file,
		//which is made with its preamble, with no start values, when it isn't there.  The file is live for good:  no
		//rewrite puts an edit in time order, since a read applies them in the order they were made.  Throws as Prepare does.
		α PrepareMods( Day day, vector<Proto::HistoryRecord>&& records, const Membership& members, SRCE )ε->Pending;
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
		//A record written at (index, time) that a later file's start value may copy:  what Corrections looks at.  Commit
		//notes each value it writes; an edit notes each value it changes once its records are written.
		α Touch( NodeIndex index, Ticks time )ι->void;
		//The start-value corrections the records touched since reach (spec *Record format*):  for each, every later file
		//that lists its node with a start value at or before its time, other than that record itself, gets the node's last
		//record through that time, as members.Last gives it, as its start value, a live file by a preamble record appended
		//and an archive by its rewrite, which the flush writes.  The walk ends at the first later file whose start value for
		//the node is past the time, since the start values of the files after it are later still.  One they can't be
		//prepared for, or whose start value members.Last can't read, is said and left as it is:  the next record or edit
		//that reaches it corrects it.  The touched records are forgotten.
		α Corrections( const Membership& members, TimePoint now, SRCE )ι->vector<std::pair<Day,DayWrite>>;
		//Of a node's indexes, those the first file after day that lists one lists:  none when no later file does.  One that
		//can't be read is passed over, as Corrections passes it.  Throws when the days can't be listed.
		α ListedAfter( Day day, const vector<NodeIndex>& indexes, SRCE )ε->vector<NodeIndex>;
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
		α Present()Ι->Day{ return _present; }
		α Find( Day day )Ι->const DayFile*;//none until the process opens it.
		//What a read serves of a day's file:  an archive whole, a live file through its runs, its preamble, and the file,
		//open.  As the process knows it once it has opened the file, else as a scan of it finds it, which is a read of the
		//preamble alone for an archive.  None for a day with no file, one the process knew of that has since gone included, or with nothing
		//kept, which a scan logs.  Opened under the files lock with the rest, so a rewrite that renames over the day after
		//leaves the read the file they describe.  Throws, at Error, when the file can't be opened or scanned.
		struct Served final{ sp<IO::OpenFile> File; uint32_t Generation{}; uint Size{}; vector<Run> Runs; Hist::Preamble Preamble; };
		α Serve( Day day, SL sl )Ε->optional<Served>;
		//The day's modifications file the same way:  none for a day with none.  A read applies them to the day's values, and
		//a modified read returns them.  The first serve opens it, as an append would, so it is scanned once:  a
		//modifications file is live for good.
		α ServeMods( Day day, SL sl )ε->optional<Served>;
		α LastFlush()ι->IO::Records::Flushed&{ return _flushed; }
	private:
		//A value file's, or with mods the day's modifications file's.
		α Open( Day day, SL sl, const std::function<void( Proto::HistoryRecord& )>& restore={}, bool mods=false )ε->DayFile&;
		α Serve( const std::map<Day,DayFile>& files, const fs::path& path, Day day, SL sl )Ε->optional<Served>;
		//The run Prepare and PrepareMods hand out:  a file with nothing kept opens with its FileStart and preamble, then run
		//follows, each sealed.  What Commit takes of it is on the Pending; the file notes the append as outstanding.
		α Append( DayFile& file, Day day, bool mods, bool existed, bool restart, vector<Proto::HistoryRecord>&& preamble, vector<Proto::HistoryRecord>&& run, NodeIndex nextIndex )Ι->Pending;
		α Past( Day day, TimePoint now )Ι->bool;//whether day's midnight rewrite is due:  it ended `delay` ago.
		α File( Day day )Ι->fs::path;
		α ModsFile( Day day )Ι->fs::path;
		α Temp( Day day )Ι->fs::path;//what a rewrite of day's file writes, beside it.
		α Restore( Proto::HistoryRecord& r, flat_map<NodeIndex,ExNodeId>& members, Day day )ι->void;
		//A value, or a NodeAdded's start value, into _last:  a start value stands in for one the same day's preamble gave,
		//as a correction does, and otherwise only for an older one, as a value does.
		α Fold( Proto::HistoryRecord& r, Day day )ι->void;
		α Newer( Proto::DataValue&& stored, optional<Day> preamble={} )ι->void;
		//A member's preamble record for a file of day:  with its start value, its last record before the day, from _last or
		//the walk back, unless withStart is false, as a modifications file's take none.  Throws when the walk back can't read
		//a file through.
		α Added( NodeIndex index, const Membership::Node& node, Ticks start, const Membership& members, bool withStart=true )Ε->Proto::HistoryRecord;
		//A later file's start values:  one the process holds, by its entry, an archive by its preamble alone, read once into
		//archives, and a live file by its first-open scan, which its next append needs anyway.  None for one that can't be
		//read, or that the historian won't write to:  it can't be corrected, nor say what the files after it copy.
		α StartsOf( Day day, std::map<Day,optional<Hist::Preamble>>& archives, SL sl )ι->const Hist::Preamble*;

		const fs::path _root;
		const string _name;
		const std::chrono::time_zone& _tz;
		const Duration _delay;
		IO::Records::Flushed _flushed;
		Restored _restored;
		std::map<Day,DayFile> _files;//an archive's only while _rewritten holds its day:  one merged into later is scanned again.
		std::map<Day,DayFile> _mods;//each modifications file the process has opened.
		std::map<Day,up<Exception>> _failedOpenings;//each the flush's Opening couldn't read, for the day's next Open to throw.
		vector<std::pair<NodeIndex,Ticks>> _touched;//each record written since Corrections last looked, by node and time.
		vector<Day> _edited;
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
		//Each node's newest stored value, a heartbeat or a marker included, as the edits leave it, which a new file's
		//preamble takes as its start value when it is from before the file's day.  Preamble is the day whose preamble gave
		//it, while the start read folds files:  a later preamble record of that day replaces it, where a value only moves
		//it on.
		struct Last final{ Proto::DataValue Value; optional<Day> Preamble; };
		absl::flat_hash_map<NodeIndex,Last> _last;
	};
}