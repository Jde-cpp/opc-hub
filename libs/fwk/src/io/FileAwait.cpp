#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/Cache.h>

#define let const auto
namespace Jde{
	uint32 _chunkSize;
	α IO::ChunkByteSize()ι->uint32{ return _chunkSize; }
	uint8 _threadSize;
	α IO::ThreadSize()ι->uint8{ return _threadSize; }
	α IO::Init()ι->void{
		_chunkSize = Settings::FindNumber<uint32>("/workers/io/chunkByteSize").value_or(1 << 19);
		_threadSize = Settings::FindNumber<uint8>("/workers/io/threads").value_or(5);
#ifndef _MSC_VER
		LinuxInit();
#endif
	}

namespace IO{
	α IFileChunkArg::Handle()Ι->HFile&{ return _fileIOArg->Handle; }
	α IFileChunkArg::IsRead()Ι->bool{ return _fileIOArg->IsRead; }

	FileIOArg::FileIOArg( fs::path path, ReadOptions options, bool vec, SL sl )ι:
		Offset{ options.Offset },
		Limit{ options.Size },
		IsRead{ true },
		Path{ move(path) },
		_sl{ sl },
		_tags{ options.Tags }{
		if( vec )
			Buffer = vector<byte>{};
	}
	FileIOArg::FileIOArg( fs::path path, variant<string,vector<byte>> data, WriteOptions options, SL sl )ι:
		Buffer{ move(data) },
		Offset{ options.Offset },
		Mode{ options.Mode },
		Sync{ options.Sync },
		IsRead{ false },
		Path{ move(path) },
		_sl{ sl },
		_tags{ options.Tags }
	{}

	α FileIOArg::SizeBuffer( uint fileSize )ε->void{
		let size = fileSize>Offset ? std::min( Limit.value_or(fileSize), fileSize-Offset ) : 0;//a range that starts at or past the end reads nothing.
		visit( [size](auto&& b){ b.resize(size); }, Buffer );
	}
	α FileIOArg::CheckPrefix( uint fileSize )ε->void{
		if( fileSize>=Offset )
			return;
		//cutting a shorter file to Offset grows it with zeros, which would then pass for the kept bytes.
		IOException e{ Path, Ƒ("Truncate offset {} is past the end of the file, {} bytes", Offset, fileSize), _sl };
		e.Error = EIOError::Invalid;
		e.Throw();
	}

	α FileIOArg::PostExp( up<IFileChunkArg>&& chunk, uint32 code, string&& m, bool written )ι->void{
		{
			lg l{ ChunkMutex };
			while( Chunks.size() )
				Chunks.pop();
		}
		//one arm for both handle types - the read/write promises share ResumeExp(Exception&&, h).  CoHandle already
		//nulled _coHandle under _coHandleMutex; a null handle means the awaiter was already resumed, nothing to post.
		visit( [&]( auto h ){
			if( h )
				Post( [path=move(Path), sl=_sl, m=move(m), code, written, h](){
					IO::IOException e{ path, code, move(m), sl };
					e.Written = written;
					h.promise().ResumeExp( move(e), h );
				} );
		}, CoHandle() );
		chunk=nullptr;
	}

	α FileIOArg::ResumeComplete()ι->void{
		visit( [this]( auto h ){
			constexpr bool isRead = std::is_same_v<decltype(h), StringAwait::Handle>;
			if( !h ){//a read loses its handle to PostExp when an earlier chunk failed while a later one was still in flight - that
				if constexpr( !isRead )//completion is expected & silent.  A write completes only once every chunk has, so a missing handle is a bug.
					CRITICAL( "[{}]no handle.", Path.string() );
				return;
			}
			if constexpr( isRead )
				Post( get<string>(move(Buffer)), move(h) );
			else
				Post( move(h) );
		}, CoHandle() );
	}

	α FileIOArg::ResumeExp( uint32 code, string&& m )ι->void{
		lg l{ ChunkMutex };
		ResumeExp( code, move(m), l );
	}
	α FileIOArg::ResumeExp( uint32 code, string&& m, lg& /*chunkLock*/ )ι->void{
		IOException e{ Path, code, move(m), _sl };
		visit( [&e]( auto h ){ h.promise().ResumeExp( move(e), h ); }, CoHandle() );//same contract as before: the caller holds a live handle.
	}

	α ReadAwait::await_ready()ι->bool{
		if( auto p = _cache ? Cache::Get<string>(_arg->Path.string()) : nullptr; p ){
			_arg->Buffer = *p;
			_fromCache = true;
			return true;
		}
		else{
			try{
				_arg->Open( false );
			}
			catch( IOException& e ){
				ExceptionPtr = e.Move();
			}
		}
		return ExceptionPtr!=nullptr;
	}

	α WriteAwait::await_ready()ι->bool{
		if( _arg->Mode==EWriteMode::Append && _arg->Offset ){//an At that lost its Mode - appending instead would grow the file on every write.
			auto e = mu<IOException>( _arg->Path, "Append takes no Offset - EWriteMode::At writes at one", _arg->_sl );
			e->Error = EIOError::Invalid;
			ExceptionPtr = move( e );
			return true;
		}
		try{
			_arg->Open( _create );
		}
		catch( IOException& e ){
			ExceptionPtr = e.Move();
		}
		return ExceptionPtr!=nullptr;
	}

	α ReadAwait::Suspend()ι->void{
		DBGT( _arg->_tags, "ReadAwait::Suspend: {}, size: {}", _arg->Path.string(), _arg->Size() );
		_arg->Send( _h );
	}
	α WriteAwait::Suspend()ι->void{
		DBGT( _arg->_tags, "WriteAwait::Suspend: {}, size: {}", _arg->Path.string(), _arg->Size() );
		_arg->Send( _h );
	}
	α ReadAwait::await_resume()ε->string{
		if( ExceptionPtr )
			ExceptionPtr->Throw();
		DBGT( _arg->_tags, "ReadAwait::Complete: {}, size: {}", _arg->Path.string(), _arg->Size() );
		//The failure of a suspended read lives in the promise - PostExp/ResumeExp put it there - and Open sized Buffer to the
		//file *before* the io ran, so a read that failed still has a full-length buffer the io never filled.  Testing r.size()
		//first therefore reported every such failure as a successful read of that many zero bytes; reading a directory
		//returned 200 of them instead of throwing EISDIR.  CheckException keeps the concrete IOException type, and the null
		//check is for the ready paths (cache hit, or Open threw): they never suspend, so there is no promise at all.
		if( auto promise = Promise(); promise && promise->Exp() )
			CheckException();
		auto& r = get<string>(_arg->Buffer);
		if( _fromCache || r.size() )//an empty cache hit must not fall through - StringAwait has no promise on the ready path.
			return move(r);
		auto y = StringAwait::await_resume();
		if( _cache )
			Cache::Set<string>( _arg->Path.string(), y );
		return y;
	}
	α WriteAwait::await_resume()ε->void{
		if( ExceptionPtr )
			ExceptionPtr->Throw();
		DBGT( _arg->_tags, "WriteAwait::Complete: {}, size: {}", _arg->Path.string(), _arg->Size() );
		VoidAwait::await_resume();
	}
}}