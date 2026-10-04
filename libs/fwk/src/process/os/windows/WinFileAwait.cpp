#include <jde/fwk/io/FileAwait.h>
#include <cerrno>
#include <boost/asio.hpp>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/str.h>

#define let const auto

namespace Jde::IO{
	namespace asio = boost::asio;
	constexpr ELogTags _tags{ ELogTags::IO };
	using RandomAccessHandle=asio::windows::random_access_handle;//owns the file HANDLE & registers it with the executor's iocp; closed on the op's terminal path, destroyed when the last chunk handler releases it.

	struct WinChunk final : IFileChunkArg{
		WinChunk( sp<FileIOArg> arg, uint index )ι:
			IFileChunkArg{ arg, index },
			StartIndex{ Index*ChunkByteSize() },
			Bytes{ std::min(StartIndex+ChunkByteSize(), FileArg()->Size())-StartIndex },
			FileOffset{ arg->Offset+StartIndex }
		{}
		uint StartIndex;//offset into Buffer - partial transfers advance this together with FileOffset.
		uint Bytes;//bytes still to transfer for this chunk.
		uint FileOffset;//explicit file offset - the op's Offset + StartIndex; an append's Offset is the EOF at Open. Never eof (offset -1) semantics: chunks past the first would land at the wrong offset & parallel chunks would interleave.
	};

	FileIOArg::~FileIOArg(){}//HandlePtr self-closes when Send never ran (cache hit / open failure); after Send the asio handle owns the close.

	α FileIOArg::Open( bool create )ε->void{
		const DWORD access = IsRead ? GENERIC_READ : GENERIC_WRITE;
		//Every open shares all three, as linux does.  FILE_SHARE_DELETE so a whole-file rewrite can rename its temp over a file being read (ArchiveFileAwait::Save): without it MoveFileEx cannot take DELETE on the target and the replace fails, which would only move the sharing violation from the reader to the writer.  The open handle goes on reading the version it opened, as it would on linux.  FILE_SHARE_READ and FILE_SHARE_WRITE so a reader and an appender can hold one file: each open has to admit the access the other already has.
		const DWORD sharing = FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
		let append = Mode==EWriteMode::Append;
		let replace = Mode==EWriteMode::Truncate && !Offset;
		//replace must drop the old contents: chunks write from offset 0, so OPEN_EXISTING would leave a longer previous version's tail behind.  create picks whether a missing file is an error, matching linux's O_CREAT.  At, and a Truncate that keeps a prefix, open what is there.
		const DWORD creationDisposition = IsRead ? OPEN_EXISTING
			: append ? OPEN_ALWAYS
			: replace ? ( create ? CREATE_ALWAYS : TRUNCATE_EXISTING )
			: ( create ? OPEN_ALWAYS : OPEN_EXISTING );
		const DWORD dwFlagsAndAttributes = IsRead ? FILE_FLAG_SEQUENTIAL_SCAN : FILE_ATTRIBUTE_ARCHIVE;
		auto tmp = Str::Replace( Path.string(), '/', '\\' );
		let path = string{"\\\\?\\"}+tmp;
		Handle = HandlePtr( WinHandle(::CreateFile(path.c_str(), access, sharing, nullptr, creationDisposition, FILE_FLAG_OVERLAPPED | dwFlagsAndAttributes, nullptr), [&](){
			return IOException( Path, GetLastError(), "CreateFile" );//copy, not move: keep Path for later logging on this object.
		}) );
		LARGE_INTEGER fileSize;
		if( IsRead ){
			THROW_IFX( !::GetFileSizeEx(Handle.get(), &fileSize), IOException(Path, GetLastError(), "GetFileSizeEx") );
			let total = (uint)fileSize.QuadPart;
			let size = total>Offset ? std::min( Limit.value_or(total), total-Offset ) : 0;//a range that starts at or past the end reads nothing.
			std::visit( [size](auto&& b){b.resize(size);}, Buffer );
		}
		else if( append ){//chunks write at explicit offsets - capture the base here. (a file is appended by one process at a time - eof-offset semantics couldn't handle multiple chunks anyway.)
			THROW_IFX( !::GetFileSizeEx(Handle.get(), &fileSize), IOException(Path, GetLastError(), "GetFileSizeEx") );
			Offset = (uint)fileSize.QuadPart;
		}
		else if( Mode==EWriteMode::Truncate && Offset ){//keeps the first Offset bytes.  By handle, not SetEndOfFile: an overlapped handle has no file pointer to set it from.
			FILE_END_OF_FILE_INFO end;
			end.EndOfFile.QuadPart = (LONGLONG)Offset;
			THROW_IFX( !::SetFileInformationByHandle(Handle.get(), FileEndOfFileInfo, &end, sizeof(end)), IOException(Path, GetLastError(), "SetFileInformationByHandle") );
		}
		TRACE( "[{}]{} size={}", Path.string(), IsRead ? "Read" : "Write", Size() );
	}

	Ω start( const sp<RandomAccessHandle>& h, sp<IFileChunkArg>&& chunk )ι->void;
	Ω startNext( const sp<RandomAccessHandle>& h, const sp<FileIOArg>& op )ι->void{
		sp<IFileChunkArg> chunk;
		{
			lg l{ op->ChunkMutex };
			if( !op->Chunks.size() )
				return;//all chunks started, or an error path cleared the queue.
			chunk = sp<IFileChunkArg>{ move(op->Chunks.front()) };
			op->Chunks.pop();
		}
		start( h, move(chunk) );
	}

	Ω resumeCompleted( const sp<RandomAccessHandle>& h, const sp<FileIOArg>& op )ι->void{//final chunk completed.
		//FlushFileBuffers has no overlapped form, so it holds this executor thread until the device has the data.
		const DWORD syncError = op->Sync && !::FlushFileBuffers( h->native_handle() ) ? ::GetLastError() : 0;
		boost::system::error_code ec;
		h->close( ec );//before the resume posts, so the coroutine never finds its own handle still open. Safe inline - the queue is empty, so no initiation can race the close.
		if( ec )
			WARN( "[{}]close failed: {}", op->Path.string(), ec.message() );
		if( syncError )
			op->PostExp( up<IFileChunkArg>{}, (uint32)syncError, "FlushFileBuffers failed\n" );
		else
			op->ResumeComplete();
	}

	Ω onChunk( const sp<RandomAccessHandle>& h, const sp<IFileChunkArg>& chunk, const boost::system::error_code& ec, uint bytes )ι->void{
		WinChunk& wchunk = dynamic_cast<WinChunk&>( *chunk );
		auto op = chunk->FileArg();
		if( bytes==wchunk.Bytes ){//chunk complete.
			if( op->ChunksToSend>++op->ChunksCompleted )
				startNext( h, op );
			else
				resumeCompleted( h, op );
		}
		else if( bytes ){//partial transfer - resubmit the remainder; a hard error resurfaces on the resubmit.
			TRACE( "Partial {}: {}, index: {}, completed: {} of {} - resubmitting remainder.", chunk->IsRead() ? "read" : "write", op->Path.string(), chunk->Index, bytes, wchunk.Bytes );
			wchunk.StartIndex += bytes;
			wchunk.FileOffset += bytes;
			wchunk.Bytes -= bytes;
			start( h, sp<IFileChunkArg>{chunk} );
		}
		else{//error or zero progress - resume with the exception; the close cancels the op's other in-flight chunks (they land here with operation_aborted & no-op: the queue is cleared & the handle nulled).
			PostIO( [h](){ boost::system::error_code closeEc; h->close(closeEc); } );//via the strand - a close may not race an initiation.
			op->PostExp( up<IFileChunkArg>{}, ec ? (uint32)ec.value() : (uint32)EIO, Ƒ("{} index: {} failed with {} bytes remaining: {}\n", chunk->IsRead() ? "read" : "write", chunk->Index, wchunk.Bytes, ec ? ec.message() : "no progress") );
		}
	}

	Ω start( const sp<RandomAccessHandle>& h, sp<IFileChunkArg>&& chunk )ι->void{
		PostIO( [h, chunk=move(chunk)](){//initiations are strand-serialized - asio handles aren't safe for concurrent calls on the same object.
			if( !h->is_open() )//an error path closed the handle after this initiation was queued.
				return;
			WinChunk& wchunk = dynamic_cast<WinChunk&>( *chunk );
			auto op = chunk->FileArg();
			TRACE( "({}){} chunk {}: offset {}, {} bytes.", op->Path.string(), chunk->IsRead() ? "Reading" : "Writing", chunk->Index, wchunk.FileOffset, wchunk.Bytes );
			auto handler = [h, chunk]( const boost::system::error_code& ec, size_t bytes ){ onChunk( h, chunk, ec, (uint)bytes ); };
			if( chunk->IsRead() )
				h->async_read_some_at( wchunk.FileOffset, asio::buffer(op->Data()+wchunk.StartIndex, wchunk.Bytes), move(handler) );
			else
				h->async_write_some_at( wchunk.FileOffset, asio::buffer(op->Data()+wchunk.StartIndex, wchunk.Bytes), move(handler) );
		} );
	}

	α FileIOArg::Send( HCo h )ι->void{
		{
			lg l{ _coHandleMutex };
			_coHandle = h;
		}
		auto self = shared_from_this();
		let totalBytes = Size();
		let chunkByteSize = ChunkByteSize();
		{
			lg l{ ChunkMutex };
			ChunksToSend = (totalBytes+chunkByteSize-1)/chunkByteSize;//ceil( totalBytes / chunkByteSize )
			for( uint i=0; i*chunkByteSize<totalBytes; ++i )
				Chunks.emplace( mu<WinChunk>(self, i) );
		}
		TRACE( "[{}] chunks = {}", Path.string(), ChunksToSend );
		if( ChunksToSend==0 ){//empty file - no completions will arrive; resume immediately.
			TRACE( "[{}]Empty file - resuming without io.", Path.string() );
			if( Sync && !::FlushFileBuffers(Handle.get()) )//an empty write that syncs.
				PostExp( up<IFileChunkArg>{}, (uint32)::GetLastError(), "FlushFileBuffers failed\n" );
			else
				ResumeComplete();
			return;
		}
		auto executor = Executor();
		if( !executor ){//finalizing - the io could never run; mirror Post's drop semantics.
			WARN( "[{}]Send after executor teardown - dropping file io.", Path.string() );
			return;
		}
		HANDLE raw = Handle.release();
		sp<RandomAccessHandle> handle;
		try{
			handle = ms<RandomAccessHandle>( *executor, raw );
		}
		catch( const boost::system::system_error& e ){
			::CloseHandle( raw );
			PostExp( up<IFileChunkArg>{}, (uint32)e.code().value(), Ƒ("iocp registration failed: {}", e.what()) );
			return;
		}
		//explicit offsets make parallel chunks order-independent for reads & writes both - the append base is fixed in Open.
		let window = std::min<uint>( ChunksToSend, ThreadSize() );
		for( uint i=0; i<window; ++i )
			startNext( handle, self );
	}
}
