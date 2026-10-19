#pragma once
#include <jde/fwk/io/FileAwait.h>
#include "Appender.h"
#include "Merge.h"

#define Φ Γ auto
namespace Jde::IO::Records{
	//The write of one append:  cuts the file to offset, the bytes that stay, writes bytes after them and fsyncs.  A file
	//this makes has its directories fsynced too.
	Ξ AppendWrite( fs::path path, uint offset, string&& bytes, SRCE )ι->WriteAwait{
		return WriteAwait{ move(path), move(bytes), WriteOptions{.Create=true, .Mode=EWriteMode::Truncate, .Offset=offset, .Sync=true}, sl };
	}

	//A new file's first run, sealed on its own:  its FileStart, the prologue right after it, then its records.
	template<Record T> struct Opening final{
		Proto::FileStart Start;
		vector<T> Prologue;
		vector<T> Records;
	};
	//What BuildAppend makes:  the bytes, the size they bring the file to, the owner's state after them, and each run that
	//holds a record with a time.
	struct Appended final{
		string Bytes;
		uint End;
		State Chain;
		vector<Run> Runs;
	};
	//The bytes of one append to a file holding size bytes, the owner's state at chain:  with opening, the file's first run,
	//then run, sealed.  A record Add refuses, one too large for a file, is said at Error and left out, and the run goes on
	//without it.  note sees each record once it is in, as it was given.
	template<Record T> α BuildAppend( uint size, State chain, optional<Opening<T>>&& opening, vector<T>&& run, const std::function<void( const T& )>& note )ε->Appended{
		Appended y;
		auto add = [&]( Appender<T>& appender, Run& span, bool& timed, T&& r ){
			const auto t = Traits<T>::Time( r );
			if( !appender.TryAdd(move(r), note) || !t )
				return;
			span.First = timed ? std::min( span.First, *t ) : *t;
			span.Last = timed ? std::max( span.Last, *t ) : *t;
			timed = true;
		};
		if( opening ){
			Appender<T> first{ y.Bytes, chain };
			first.Start( move(opening->Start) );
			Run span{ .Offset=size, .End=0, .Chain=chain, .First=0, .Last=0 };
			bool timed{};
			for( auto& r : opening->Prologue )
				add( first, span, timed, move(r) );
			for( auto& r : opening->Records )
				add( first, span, timed, move(r) );
			chain = first.Seal();
			if( timed ){
				span.End = size+(uint)y.Bytes.size();
				y.Runs.push_back( span );
			}
		}
		Appender<T> appender{ y.Bytes, chain };
		Run span{ .Offset=size+(uint)y.Bytes.size(), .End=0, .Chain=chain, .First=0, .Last=0 };
		bool timed{};
		for( auto& r : run )
			add( appender, span, timed, move(r) );
		y.Chain = appender.Seal();
		y.End = size+(uint)y.Bytes.size();
		if( timed ){
			span.End = y.End;
			y.Runs.push_back( span );
		}
		return y;
	}

	enum class EPart : uint8{ Ready, More, Done };//Ready:  Write the part.  More:  Wants/Feed.  Done:  the last part is written.
	//A file written whole as its archive:  the runs of the live file it was, or the archive it already is, merged in time
	//order with late, a flush's records for its day.  The FileStart's generation is one past the file's, the prologue
	//follows it, then the head - the records the owner says open the file, folded as it says - and no checkpoint is carried
	//over.  It goes to a temp file beside the file in parts, so neither the file nor the day is ever held whole, and the
	//owner renames that over the file once Done.
	template<Record T> struct Rewrite final : noncopyable{
		struct Hooks final{
			std::function<bool( const T& )> IsHead;//a record of the head, consumed into it:  the first that isn't begins the body.
			std::function<void( vector<T>& )> FoldHead;//the owner's fold of the head, once it is whole.
			std::function<void( const T& )> NoteHead;//sees each record of the folded head once it is written, as it was folded.
		};
		//start is the FileStart to write, its generation the file's +1.  file is open when there are runs.
		Rewrite( fs::path path, fs::path temp, sp<OpenFile> file, vector<Run> runs, vector<T> late, Proto::FileStart start, vector<T> prologue, Hooks hooks, uint partBytes=PartBytes, SRCE )ι:
			_path{ move(path) }, _temp{ move(temp) }, _records{ move(file), move(runs), move(late), sl }, _start{ move(start) }, _prologue{ move(prologue) }, _hooks{ move(hooks) }, _partBytes{ partBytes }{}
		//Makes the temp file's next part.  Throws when the file it merges can't be read through.
		α Next()ε->EPart;
		α Wants()Ι->optional<Window>{ return _records.Wants(); }
		α Feed( string&& bytes )ι->void{ _records.Feed( move(bytes) ); }
		α File()Ι->const sp<OpenFile>&{ return _records.File(); }
		//Writes the part.  The first makes the temp file, or cuts one a crash left, and the last fsyncs it.
		α Write( SRCE )ι->WriteAwait{
			const auto first = !std::exchange( _written, true );
			_size += (uint)_part.size();
			return WriteAwait{ _temp, std::exchange(_part, {}), WriteOptions{.Create=first, .Mode=first ? EWriteMode::Truncate : EWriteMode::Append, .Sync=_last}, sl };
		}
		α Unreadable()Ι->bool{ return _records.Unreadable(); }//the file no longer reads as its scan did.
		α Size()Ι->uint{ return _size; }//written so far.
		α Start()Ι->const Proto::FileStart&{ return _start; }
		α Path()Ι->const fs::path&{ return _path; }
		α Temp()Ι->const fs::path&{ return _temp; }
	private:
		fs::path _path;
		fs::path _temp;
		Merge<T> _records;
		Proto::FileStart _start;
		vector<T> _prologue;
		Hooks _hooks;
		uint _partBytes;
		uint _size{};
		string _part;
		Appender<T> _writer{ _part, 0 };//never sealed:  an archive carries no checkpoint.
		vector<T> _head;
		optional<T> _ahead;//the record after the part's last, so the last part is known as it is made.
		bool _opened{};
		bool _headDone{};
		bool _reading{};//a Next of the merge is owed, its record to be _ahead.
		bool _written{};//the temp file is made.
		bool _last{};
	};

	template<Record T> α Rewrite<T>::Next()ε->EPart{
		if( _last )
			return EPart::Done;
		if( !std::exchange(_opened, true) ){
			_writer.Start( _start );
			for( auto& r : _prologue )
				_writer.TryAdd( move(r) );//one too large for a file is left out, as an append leaves it.
			_prologue.clear();
		}
		T r;
		if( !_headDone ){
			for( ;; ){
				const auto next = _records.Next( r );
				if( next==ENext::More )
					return EPart::More;
				if( next==ENext::End )
					break;
				if( !_hooks.IsHead || !_hooks.IsHead(r) ){
					_ahead = move( r );
					break;
				}
				_head.push_back( move(r) );
			}
			if( _hooks.FoldHead )
				_hooks.FoldHead( _head );
			for( auto& h : _head )
				_writer.TryAdd( move(h), _hooks.NoteHead );
			_head.clear();
			_headDone = true;
		}
		for( ;; ){
			if( _ahead && !_reading ){
				_writer.TryAdd( move(*_ahead) );
				_ahead.reset();
				_reading = true;
			}
			if( _reading ){
				const auto next = _records.Next( r );
				if( next==ENext::More )
					return EPart::More;
				_reading = false;
				if( next==ENext::Record )
					_ahead = move( r );
			}
			if( !_ahead ){
				_last = true;
				return EPart::Ready;
			}
			if( _part.size()>=_partBytes )
				return EPart::Ready;
		}
	}
}
#undef Φ