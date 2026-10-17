#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/io/records/FileState.h>
#include <jde/fwk/io/records/Rewrite.h>
#include <jde/app/proto/Log.pb.h>

#define Φ Γ auto
namespace Jde::App::Log{
	using Proto::FileEntry;
	//The framework's record layer (include/jde/fwk/io/records) over the log's record (Log.proto's FileEntry):  its
	//FileStart is the oneof's field 4 and its Checkpoint field 5.  The layer frames, scans, merges and rewrites; what is
	//the log's own stays here:  the daily file's state and the archives' Merged mark.  A file that isn't the layer's, an
	//earlier build's included, is what the layer makes of any such file:  refused, or dropped as a torn start.
	constexpr uint32_t StartField{ 4 };
	constexpr uint32_t CheckpointField{ 5 };
	using IO::Records::Run;
	using IO::Records::Window;
	using IO::Records::ENext;
	using IO::Records::EStop;

	//The layer's time for the log:  ns since the epoch.
	Ξ Ns( TimePoint t )ι->int64_t{ return std::chrono::duration_cast<std::chrono::nanoseconds>( t.time_since_epoch() ).count(); }
	Ξ Ns( const google::protobuf::Timestamp& t )ι->int64_t{ return t.seconds()*1'000'000'000+t.nanos(); }
	//A record's time:  an entry's, and none for a String, a Merged or one of the layer's.
	Φ Time( const FileEntry& r )ι->optional<int64_t>;
}
namespace Jde::IO::Records{
	template<> struct Traits<App::Log::FileEntry>{
		using R = App::Log::FileEntry;
		static constexpr uint32_t StartField{ App::Log::StartField };
		static constexpr uint32_t CheckpointField{ App::Log::CheckpointField };
		Ω Known( uint32_t field )ι->bool{ return field==R::kEntry || (field>=R::kStr && field<=R::kMerged); }
		Ω Time( const R& r )ι->optional<Records::Time>{ return App::Log::Time( r ); }
		Ω ToDisk( R&, State& )ι->void{}//the log's times are absolute on disk.
		Ω ToMemory( R&, State& )ι->void{}
		Ω SetStart( R& r, const Proto::FileStart& f )ι->void{ *r.mutable_file_start() = f; }
	};
}
namespace Jde::App::Log{//after the traits, which the layer's templates are constrained on.
	using Appender = IO::Records::Appender<FileEntry>;
	using Reader = IO::Records::Reader<FileEntry>;
	using Rewrite = IO::Records::Rewrite<FileEntry>;

	//One append to the daily file, a run, between DailyFile::Prepare and Commit.
	struct Pending final{
		fs::path Path;
		uint Offset{};//the file's bytes that stay, which the run follows.
		string Bytes;
		bool Restart{};//the file was removed since, so what was known of it goes.
		uint End{};
		IO::Records::State Chain{};
		vector<Run> Runs;
		int64_t Identity{};//the file's, which the run's FileStart sets when it opens the file.
		//Cuts the file to Offset and writes the run after it.  Not fsynced:  the log is best effort, as its text file
		//is, and a checkpoint tells a torn flush apart whether or not the bytes reached the device.
		α Write( SRCE )ι->IO::WriteAwait;
	};
	//The daily file as the process knows it:  the record layer's state of it, from its first-open scan and from its
	//own appends since, under the file's LockKey, which every flush, round and query takes.
	struct Γ DailyFile final : IO::Records::FileState{
		bool Opened{};//scanned, converted or made by this process:  until then nothing is known of it.
		int64_t Identity{};//its FileStart's ts, which names it to the archives (Log.proto's Merged); 0 until it is made.
		//The whole records of a torn last append, which the scan found past the last checkpoint:  the next append writes
		//them again, so a tear loses only the record it cut, and a query sees them meanwhile.
		vector<FileEntry> Salvaged;
		//The run that writes records after what is known, Salvaged first:  a file with nothing kept opens with its FileStart.
		//Cuts a torn tail the scan found first.  Throws when the file holds other bytes than the log wrote, forgetting
		//what it knew so the next write scans it again, and when the log won't write to it (Refused), at Error the first
		//time and at Debug after.
		α Prepare( vector<FileEntry> records, SRCE )ε->Pending;
		//Once the run's Write has returned.
		α Commit( Pending&& run )ι->void;
		//Once an archive round has removed the file:  nothing is known of it but Salvaged, which no append has written.
		α Removed()ι->void;
	};

	//The daily file's first open in the process:  the layer's scan, fed a window at a time (More:  Wants/Feed), which
	//keeps the file through its last good checkpoint and salvages the whole records after it.  Nothing to read when the
	//file isn't there.  Throws when it is there and can't be read through.
	struct Γ Opening final : noncopyable{
		Opening( fs::path path, SRCE )ε;
		~Opening();
		α Next()ε->ENext;//More:  Wants/Feed.  End:  Take.  Never Record.
		α Wants()Ι->optional<Window>;
		α Feed( string&& bytes )ι->void;
		α File()Ι->const sp<IO::OpenFile>&{ return _file; }
		α Take()ι->DailyFile{ return move( _y ); }
	private:
		struct Decoder;

		DailyFile _y;
		sp<IO::OpenFile> _file;
		up<Decoder> _decoder;
		optional<IO::Records::Scanner> _scanner;
	};
	//The first open through the file awaitables, for a coroutine of any task type.
	struct Γ DailyOpenAwait final : AnyAwait<DailyFile>{
		DailyOpenAwait( fs::path path, SRCE )ι:AnyAwait<DailyFile>{ sl }, _path{ move(path) }{}
		α Suspend()ι->void override{ Open(); }
	private:
		α Open()ι->VoidTask;
		fs::path _path;
	};
	//Every record of runs, in file order, read through the file awaitables, the layer's two records left out.  Throws,
	//at Error, when a run can't be read through:  the file no longer reads as its scan did.
	struct Γ RunsReadAwait final : AnyAwait<vector<FileEntry>>{
		RunsReadAwait( sp<IO::OpenFile> file, vector<Run> runs, SRCE )ι:AnyAwait<vector<FileEntry>>{ sl }, _file{ move(file) }, _runs{ move(runs) }{}
		α Suspend()ι->void override{ Read(); }
	private:
		α Read()ι->VoidTask;
		sp<IO::OpenFile> _file;
		vector<Run> _runs;
	};
	//Every record of bytes, a whole file read already, the layer's two records left out.  Throws as RunsReadAwait does.
	Φ ParseRecords( sv bytes, const fs::path& path, SRCE )ε->vector<FileEntry>;

	//What an archive opens with:  its FileStart, and the Merged after it.
	struct ArchiveHead final{
		IO::Proto::FileStart Start;
		optional<Proto::Merged> Merged;
	};
	constexpr uint ArchiveHeadBytes{ 64 };//holds both at their widest.
	//From a file's first bytes:  none for one that isn't the layer's.
	Φ HeadOf( sv bytes )ι->optional<ArchiveHead>;
}
#undef Φ