#include <jde/fwk/io/FileAwait.h>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/post.hpp>
#include "FileOp.h"

#define let const auto
namespace Jde{
	struct IoStrand{
		IoStrand( sp<boost::asio::io_context> ioc )ι: Ioc{ move(ioc) }, Strand{ *Ioc }{}
		sp<boost::asio::io_context> Ioc;//keeps the io_context alive while a poster holds the strand.
		boost::asio::io_context::strand Strand;
	};
	ABSL_CONST_INIT absl::Mutex _strandMutex{ absl::kConstInit };
	sp<IoStrand> _strand ABSL_GUARDED_BY(_strandMutex);
	α IO::Internal::PostIO( absl::AnyInvocable<void()> f )ι->void{
		sp<IoStrand> strand;
		{
			ul _{ _strandMutex };
			if( !_strand && Execution::Running() )
				_strand = ms<IoStrand>( Executor() );
			strand = _strand;
		}
		if( strand )//null once the executor has stopped - the work could never run anyway.
			boost::asio::post( strand->Strand, std::move(f) );
	}
	uint32 _chunkSize;
	α IO::ChunkByteSize()ι->uint32{ return _chunkSize; }
	uint8 _windowSize;
	α IO::WindowSize()ι->uint8{ return _windowSize; }
	α IO::Init()ε->void{
		Process::AddFinalizeFunction( [](bool){ ul _{ _strandMutex }; _strand = nullptr; } );//after the join:  io still in flight during the stop posts through it.
		_chunkSize = Settings::FindNumber<uint32>("/workers/io/chunkByteSize").value_or(1 << 19);//512 KB.
		_windowSize = Settings::FindNumber<uint8>("/workers/io/threads").value_or(5);
#ifndef _MSC_VER
		IO::Internal::LinuxInit();
#endif
	}
	IO::OpenFile::OpenFile( fs::path path, up<Impl> impl )ι:_path{ move(path) }, _impl{ move(impl) }{}
	IO::OpenFile::~OpenFile()=default;//Impl's:  on windows the asio handle, then the HandlePtrs; on linux its destructor.
	α IO::OpenFile::Native()Ι->Impl&{ return *_impl; }

namespace IO::Internal{
	Chunk::Chunk( sp<FileOp> op, uint index )ι:
		Op{ move(op) },
		Index{ index },
		Start{ index*ChunkByteSize() },
		Bytes{ std::min<uint>(Start+ChunkByteSize(), Op->Size())-Start },
		Offset{ Op->Offset+Start }
	{}

	FileOp::FileOp( bool isRead, fs::path path, uint offset, ELogTags tags, SL sl )ι:
		Offset{ offset },
		IsRead{ isRead },
		Path{ move(path) },
		_sl{ sl },
		_tags{ tags }
	{}
	atomic<uint> _inFlight{};
	α FileOp::InFlight()ι->uint{ return _inFlight; }
	α FileOp::Send( sp<FileOp> self )ι->void{
		if( !Execution::Running() ){//finalizing - the io could never run, nor the resume be posted:  Post's drop semantics, on both platforms.
			WARN( "[{}]Send after executor teardown - dropping file io.", Path.string() );
			return;
		}
		let chunkByteSize = ChunkByteSize();
		ChunksToSend = (Size()+chunkByteSize-1)/chunkByteSize;
		++_inFlight;
		if( !ChunksToSend && !Syncs() ){//an empty write:  no completion will come, and nothing to sync.
			TRACE( "[{}]Empty file - resuming without io.", Path.string() );
			Complete();
			return;
		}
		Start( move(self) );
	}
	α FileOp::NextChunk()ι->optional<uint>{
		let index = _nextChunk++;
		return index<ChunksToSend ? optional<uint>{ index } : nullopt;
	}
	α FileOp::Transferred( Chunk& chunk, uint bytes )ι->ETransfer{
		if( !bytes )
			return ETransfer::NoProgress;
		if( bytes<chunk.Bytes ){
			TRACE( "Partial {}: {}, index: {}, completed: {} of {} - resubmitting remainder.", IsRead ? "read" : "write", Path.string(), chunk.Index, bytes, chunk.Bytes );
			chunk.Start += bytes; chunk.Offset += bytes; chunk.Bytes -= bytes;
			return ETransfer::Resubmit;
		}
		if( ChunksToSend>++ChunksCompleted )
			return ETransfer::Next;
		return Syncs() ? ETransfer::Sync : ETransfer::Done;
	}
	α FileOp::Synced( uint step )Ι->optional<uint>{ return step<NameDirs().size() ? optional<uint>{ step+1 } : nullopt; }
	α FileOp::Window()Ι->uint{ return std::min<uint>( ChunksToSend, WindowSize() ); }
	α FileOp::Complete( up<Exception> error )ι->void{
		if( Finished.exchange(true) )
			return;
		--_inFlight;
		if( error )
			_nextChunk = ChunksToSend;//no chunk starts after a failure:  those in flight land here, as nothing.
		PostResume( move(error) );
	}
	α FileOp::Complete( uint32 code, string&& m, bool written )ι->void{
		auto e = mu<IOException>( Path, code, move(m), _sl );
		e->Written = written;
		Complete( move(e) );
	}

	ReadOp::ReadOp( fs::path path, ReadOptions options, SL sl )ι:
		TypedOp<ReadAwait>{ true, move(path), options.Offset, options.Tags, sl },
		Limit{ options.Size }
	{}
	ReadOp::ReadOp( sp<OpenFile> file, ReadOptions options, SL sl )ι:
		TypedOp<ReadAwait>{ true, file->Path(), options.Offset, options.Tags, sl },
		Limit{ options.Size }{
		_file = move( file );
	}
	α ReadOp::Open()ε->void{
		if( !_file )
			_file = ms<OpenFile>( Path, _sl );
		SizeBuffer( _file->Size(_sl) );
		TRACE( "[{}]Read size={}", Path.string(), Size() );
	}
	α ReadOp::SizeBuffer( uint fileSize )ε->void{
		let size = fileSize>Offset ? std::min( Limit.value_or(fileSize), fileSize-Offset ) : 0;//a range that starts at or past the end reads nothing.
		Buffer.resize( size );
		_bytes = Buffer;
	}
	α ReadOp::PostResume( up<Exception> error )ι->void{
		auto buffer = error ? string{} : move( Buffer );//a failed read keeps its buffer:  a chunk of it may still be in flight, landing in it.
		Post( [a=Take(), error=move(error), buffer=move(buffer)]() mutable { if( error ) a->ResumeExp( move(*error) ); else a->Resume( move(buffer) ); } );
	}

	WriteOp::WriteOp( fs::path path, variant<string,vector<byte>> data, WriteOptions options, SL sl )ι:
		TypedOp<WriteAwait>{ false, move(path), options.Offset, options.Tags, sl },
		Data{ move(data) },
		Options{ options }{
		_bytes = visit( []( auto& x ){ return std::span<char>{ (char*)x.data(), x.size() }; }, Data );
	}
	α WriteOp::Open()ε->void{
		if( Options.Mode==EWriteMode::Append && Options.Offset ){//an At that lost its Mode - appending instead would grow the file on every write.
			IOException e{ Path, "Append takes no Offset - EWriteMode::At writes at one", _sl };
			e.Error = EIOError::Invalid;
			e.Throw();
		}
		_file = OpenFile::ForWrite( Path, Options, _sl );
#ifdef _MSC_VER
		if( Options.Mode==EWriteMode::Append )//chunks write at explicit offsets, from the end the open found.  Linux's O_APPEND places them, and Offset stays 0.
			Offset = _file->Size( _sl );
#endif
		TRACE( "[{}]Write size={}", Path.string(), Size() );
	}
	α WriteOp::PostResume( up<Exception> error )ι->void{
		Post( [a=Take(), error=move(error)]() mutable { if( error ) a->ResumeExp( move(*error) ); else a->Resume(); } );
	}
}
namespace IO{
	using namespace Internal;
	α Internal::CheckPrefix( const fs::path& path, uint offset, uint fileSize, SL sl )ε->void{
		if( fileSize>=offset )
			return;
		IOException e{ path, Ƒ("Truncate offset {} is past the end of the file, {} bytes", offset, fileSize), sl };
		e.Error = EIOError::Invalid;
		e.Throw();
	}

	ReadAwait::ReadAwait( fs::path path, ReadOptions options, SL sl )ι:
		AnyAwait<string>{ sl }, _op{ ms<ReadOp>(move(path), options, sl) }
	{}
	ReadAwait::ReadAwait( sp<OpenFile> file, ReadOptions options, SL sl )ι:
		AnyAwait<string>{ sl }, _op{ ms<ReadOp>(move(file), options, sl) }
	{}
	WriteAwait::WriteAwait( fs::path path, variant<string,vector<byte>> data, WriteOptions options, SL sl )ι:
		AnyVoidAwait{ sl }, _op{ ms<WriteOp>(move(path), move(data), options, sl) }
	{}

	α ReadAwait::await_ready()ι->bool{
		try{
			_op->Open();
		}
		catch( IOException& e ){
			_error = e.Move();
			return true;
		}
		if( _op->Size() )
			return false;
		_result = string{};//nothing to send:  an empty file, or a range at or past its end.
		return true;
	}

	α WriteAwait::await_ready()ι->bool{
		try{
			_op->Open();
		}
		catch( IOException& e ){
			_error = e.Move();
		}
		return _error!=nullptr;
	}

	α ReadAwait::Suspend()ι->void{
		DBGT( _op->_tags, "ReadAwait::Suspend: {}, size: {}", _op->Path.string(), _op->Size() );
		ReadOp::Send( _op, *this );
	}
	α WriteAwait::Suspend()ι->void{
		DBGT( _op->_tags, "WriteAwait::Suspend: {}, size: {}", _op->Path.string(), _op->Size() );
		WriteOp::Send( _op, *this );
	}
	α ReadAwait::await_resume()ε->string{
		DBGT( _op->_tags, "ReadAwait::Complete: {}, size: {}", _op->Path.string(), _op->Size() );
		return AnyAwait<string>::await_resume();//the error slot first, then the bytes - a failed read's buffer never reaches here.
	}
	α WriteAwait::await_resume()ε->void{
		DBGT( _op->_tags, "WriteAwait::Complete: {}, size: {}", _op->Path.string(), _op->Size() );
		AnyVoidAwait::await_resume();
	}
}}