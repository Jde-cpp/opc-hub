#pragma once
#include <jde/fwk/io/FileAwait.h>
#include "Reader.h"

#define Φ Γ auto
namespace Jde::IO::Records{
	//What the first-open scan keeps of a file.
	struct Scanned final{
		//None when nothing was kept.  Only once Truncate succeeds is the file then empty, needing its FileStart again:  one
		//that Keep() holds onto, foreign or with a newer build's records, takes none.
		optional<Proto::FileStart> Start;
		uint Size{};//through the last good checkpoint.
		uint FileSize{};//what the file holds:  past Size, a tail that a read ignores and Truncate drops, unless Keep().
		State Chain{};//where the owner's state resumes.
		vector<Run> Runs;//each append holding a record with a time.
		EStop Stop{ EStop::End };
		uint StopOffset{};//where the scan stopped.
		//Past a stop short of the file's end, where an append the owner sealed ends.  A torn flush is only ever the last
		//append, so this makes the stop damage.
		optional<uint> SealedAfter;
		//Whether what lies past Size must be kept:  not a torn flush, but damage, a newer build's records or a second
		//FileStart before an append the owner sealed (SealedAfter), or the start of a file that isn't a live one.  Truncate
		//refuses it, and so must any writer that would drop it, such as a rewrite from Runs.
		α Keep()Ι->bool;
	};

	//The first time the process opens a live file, to read it or to append:  scans it from the start, in windows the
	//caller fetches, and keeps it through the last checkpoint whose CRC matches.  It never changes the file.  An archive, a
	//FileStart generation past 0, is only ever replaced whole and carries no checkpoints, so all of it is kept, with no runs
	//or chain, and it is read through only for a decoder that consumes its records:  one it can't read through is damaged,
	//and Stop says where, short of the Size it keeps, for the caller to refuse.  The decoder sees every frame it keeps, a
	//checkpoint aside, and is told when an append's checkpoint proves them, so a first open maps and restores in the one
	//read.  The size is the open file's, so a rename over its path between the open and the scan can't pair one file's size
	//with another's bytes.  Throws at Error when the file can't be read through, which is no torn append.  Logs once when
	//it keeps less than the whole file:  at Warning for a torn flush, at Error for what must be kept.
	struct Γ Scanner final : noncopyable{
		Scanner( sp<OpenFile> file, uint32_t startField, uint32_t checkpointField, IDecoder& decoder, SRCE )ε;
		α Next()ε->ENext;//More:  Wants() says what to Feed.  End:  Result().  Never Record.
		α Wants()Ι->optional<Window>{ return _wants; }
		α Feed( string&& bytes )ι->void;
		α File()Ι->const sp<OpenFile>&{ return _file; }
		α Result()ι->Scanned{ return move(_y); }
	private:
		enum class EPhase : uint8{ Records, Sealed, Done };
		α Records()ε->bool;//false while it wants bytes.
		α Sealed()ι->bool;
		α Advance( const Raw& raw )ι->void;
		α Want( uint from, uint size )ι->void{ _wants = Window{ from, size }; }

		sp<OpenFile> _file;
		const uint32_t _startField;
		const uint32_t _checkpointField;
		IDecoder& _decoder;
		SL _sl;
		Scanned _y;
		EPhase _phase{ EPhase::Records };
		string _buffer;//the bytes from _base.
		uint _base{};
		uint _offset{};//the next frame's.
		optional<Window> _wants;
		bool _short{};
		optional<Proto::FileStart> _start;
		bool _archive{};
		uint32_t _crc{};//of the append since the last checkpoint.
		optional<Run> _run;//the append's, once it holds a record with a time.
		//A span the sealed-append search extends from a checkpoint candidate on:  each candidate whose crc matches none
		//forks one, since a false mark inside a body must not end the span a real checkpoint's crc covers.
		struct Span final{ uint32_t Crc{}; bool Spanned{}; };
		vector<Span> _spans;
		uint _done{};//how far into _buffer the spans reach.
	};
	//Before the first append to a scanned file:  truncates it to y.Size, so a torn append is dropped whole.  A first frame
	//the end of the file cuts short, or a zero byte, is a torn FileStart and is dropped too.  Throws, leaving the file as
	//it is, when y.Keep(), or when there is a tail to cut and the file no longer holds y.FileSize bytes:  y is then
	//another file's scan, or this one's from before it grew, and the cut would drop bytes it never read.
	Φ Truncate( const fs::path& file, Scanned& y, SRCE )ε->void;
	//file's FileStart, when it opens with one whose crc matches:  its generation tells an archive from a live file without
	//a scan.  Read now.
	Φ Head( const sp<OpenFile>& file, uint32_t startField, SRCE )ε->optional<Proto::FileStart>;

	//window of file, read now:  a synchronous caller's one blocking point.  A positioned read of the open file, not a wait
	//on a ReadAwait:  the caller may be on an executor thread, as a host's flush and the gateway's reads are, and the
	//awaitable's completion runs on the executor's threads, which other awaits block too, so a wait there deadlocks the
	//moment every thread is waiting.  A coroutine awaits the ReadAwait over the same file instead.
	Φ FetchNow( const sp<OpenFile>& file, Window window, SRCE )ε->string;
	//Feeds p, a Scanner, Reader, Merge or Rewrite, the window of file it wants, read now.
	Ŧ FeedNow( T& p, const sp<OpenFile>& file, SRCE )ε->void{
		const auto w = p.Wants();
		if( w )
			p.Feed( FetchNow(file, *w, sl) );
	}
	Ŧ FeedNow( T& p, SRCE )ε->void{ FeedNow( p, p.File(), sl ); }//one that holds its file.
	//A Reader's or Merge's next record, its windows read now:  false at the end.
	template<class TPull, class T> α NextNow( TPull& p, const sp<OpenFile>& file, T& r, SRCE )ε->bool{
		for( auto n = p.Next(r);; n = p.Next(r) ){
			if( n!=ENext::More )
				return n==ENext::Record;
			FeedNow( p, file, sl );
		}
	}
	template<class TPull, class T> α NextNow( TPull& p, T& r, SRCE )ε->bool{ return NextNow( p, p.File(), r, sl ); }
	//The scan, its windows read now.
	Φ ScanNow( sp<OpenFile> file, uint32_t startField, uint32_t checkpointField, IDecoder& decoder, SRCE )ε->Scanned;
}
#undef Φ