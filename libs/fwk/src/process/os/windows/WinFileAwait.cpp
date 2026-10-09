#include <jde/fwk/io/FileAwait.h>
#include <cerrno>
#include <boost/asio.hpp>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/thread.h>
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
		let append = Mode==EWriteMode::Append;
		let replace = Mode==EWriteMode::Truncate && !Offset;
		let keepsPrefix = Mode==EWriteMode::Truncate && Offset;
		//A read shares all three.  FILE_SHARE_WRITE so an appender can hold the file it reads: each open has to admit the access the other already has.  FILE_SHARE_DELETE so a whole-file rewrite can rename its temp over a file being read (the log archive round, ArchiveAwait::Execute): without it MoveFileEx cannot take DELETE on the target and the replace fails, which would only move the sharing violation from the reader to the writer.  The open handle goes on reading the version it opened, as it would on linux.
		//A write shares FILE_SHARE_WRITE, and FILE_SHARE_READ unless it replaces the file: a reader can hold a file that is appended to, written in place or cut to a prefix, but one replaced under it would size its buffer from one version and read the bytes of another.  No write shares delete, so the file isn't removed or renamed while it is written.
		const DWORD shareAll = FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
		const DWORD sharing = IsRead ? shareAll : replace ? FILE_SHARE_WRITE : FILE_SHARE_READ|FILE_SHARE_WRITE;
		//create picks whether a missing file is an error in every mode, matching linux's O_CREAT - except for a Truncate that keeps a prefix, since a file made here would have none to keep.  No disposition truncates: a Truncate sets the end of the file below, whatever its Offset.
		const DWORD creationDisposition = !IsRead && create && !keepsPrefix ? OPEN_ALWAYS : OPEN_EXISTING;
		const DWORD dwFlagsAndAttributes = IsRead ? FILE_FLAG_SEQUENTIAL_SCAN : FILE_ATTRIBUTE_ARCHIVE;
		auto tmp = Str::Replace( Path.string(), '/', '\\' );
		let path = string{"\\\\?\\"}+tmp;
		const HANDLE file = ::CreateFile( path.c_str(), access, sharing, nullptr, creationDisposition, FILE_FLAG_OVERLAPPED | dwFlagsAndAttributes, nullptr );
		const DWORD openError = ::GetLastError();//read before anything else can set it: a failure's code, or ERROR_ALREADY_EXISTS when an open that could create found the file there.
		Handle = HandlePtr( WinHandle(file, [&](){
			return IOException( Path, openError, "CreateFile" );//copy, not move: keep Path for later logging on this object.
		}) );
		let created = creationDisposition==OPEN_ALWAYS && openError!=ERROR_ALREADY_EXISTS;
		if( Sync && created ){//the directory gained the file's name, which Sync covers.  FILE_FLAG_BACKUP_SEMANTICS is what opens a directory, and FlushFileBuffers needs GENERIC_WRITE.
			let parent = Path.parent_path();
			let dirPath = string{"\\\\?\\"}+Str::Replace( parent.string(), '/', '\\' );
			const HANDLE dir = ::CreateFile( dirPath.c_str(), GENERIC_WRITE, shareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr );
			const DWORD dirError = ::GetLastError();
			NameDirs.emplace_back( WinHandle(dir, [&](){ return IOException( parent, dirError, "CreateFile" ); }) );
		}
		LARGE_INTEGER fileSize;
		if( IsRead ){
			THROW_IFX( !::GetFileSizeEx(Handle.get(), &fileSize), IOException(Path, GetLastError(), "GetFileSizeEx") );
			SizeBuffer( (uint)fileSize.QuadPart );
		}
		else if( append ){//chunks write at explicit offsets - capture the base here. (a file is appended by one process at a time - eof-offset semantics couldn't handle multiple chunks anyway.)
			THROW_IFX( !::GetFileSizeEx(Handle.get(), &fileSize), IOException(Path, GetLastError(), "GetFileSizeEx") );
			Offset = (uint)fileSize.QuadPart;
		}
		else if( Mode==EWriteMode::Truncate && ::GetFileType(Handle.get())==FILE_TYPE_DISK ){//the file ends at Offset, the bytes kept, and the data follows.  One cut for every Offset, 0 included: without it a shorter write leaves the old file's tail in place.  A device has no length to read or set.
			THROW_IFX( !::GetFileSizeEx(Handle.get(), &fileSize), IOException(Path, GetLastError(), "GetFileSizeEx") );
			let size = (uint)fileSize.QuadPart;
			CheckPrefix( size );
			if( size>Offset ){//By handle, not SetEndOfFile: an overlapped handle has no file pointer to set it from.
				FILE_END_OF_FILE_INFO end;
				end.EndOfFile.QuadPart = (LONGLONG)Offset;
				THROW_IFX( !::SetFileInformationByHandle(Handle.get(), FileEndOfFileInfo, &end, sizeof(end)), IOException(Path, GetLastError(), "SetFileInformationByHandle") );
			}
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

	//FlushFileBuffers has no overlapped form: it holds its thread until the device has the data.  So the flushes run on a
	//thread of their own, and a synced write never parks an executor thread behind the device.
	struct SyncThread final : IShutdown{
		α Post( absl::AnyInvocable<void()> f )ι->void{
			bool created{};
			{
				lg _{ _mutex };
				if( !_pool ){
					_pool = mu<asio::thread_pool>( 1 );
					asio::post( *_pool, [](){ Thread::SetName( "FileSync" ); } );
					created = true;
				}
				asio::post( *_pool, std::move(f) );//std::, as Jde::Post: absl::move makes a bare move on an AnyInvocable ambiguous.
			}
			if( created )//outside the lock: Shutdown takes it while the shutdown list is being walked.
				Execution::AddShutdown( this );
		}
		α Shutdown( bool /*terminate*/, SL )ι->void override{//the flushes queued run first - each has an awaiter waiting on it.
			up<asio::thread_pool> pool;
			{
				lg _{ _mutex };
				pool = move( _pool );
			}
			if( pool )
				pool->join();
		}
	private:
		up<asio::thread_pool> _pool; mutex _mutex;
	};
	SyncThread _syncThread;

	struct SyncError{ DWORD Code{}; sv Message; };
	//The file, then the directory Open added its name to.  Runs on _syncThread.
	Ω syncBuffers( HANDLE file, const FileIOArg& op )ι->SyncError{
		if( !::FlushFileBuffers(file) )
			return { ::GetLastError(), "FlushFileBuffers failed\n" };
		for( uint i=0; i<op.NameDirs.size(); ++i ){
			DBGT( op._tags, "[{}]Syncing directory {} of {}.", op.Path.string(), i+1, op.NameDirs.size() );
			if( !::FlushFileBuffers(op.NameDirs[i].get()) )
				return { ::GetLastError(), "FlushFileBuffers failed for the directory\n" };
		}
		return {};
	}

	Ω finish( const sp<RandomAccessHandle>& h, const sp<FileIOArg>& op, SyncError syncError )ι->void{
		boost::system::error_code ec;
		h->close( ec );//before the resume posts, so the coroutine never finds its own handle still open.  Nothing races it: every chunk has completed, so no initiation is queued.
		if( ec )
			WARN( "[{}]close failed: {}", op->Path.string(), ec.message() );
		if( syncError.Code )
			op->PostExp( up<IFileChunkArg>{}, (uint32)syncError.Code, string{syncError.Message}, true );
		else
			op->ResumeComplete();
	}
	Ω resumeCompleted( const sp<RandomAccessHandle>& h, const sp<FileIOArg>& op )ι->void{//final chunk completed.
		if( op->Sync )
			_syncThread.Post( [h, op](){ finish( h, op, syncBuffers(h->native_handle(), *op) ); } );
		else
			finish( h, op, {} );
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
			op->PostExp( up<IFileChunkArg>{}, ec ? (uint32)ec.value() : (uint32)ERROR_IO_DEVICE, Ƒ("{} index: {} failed with {} bytes remaining: {}\n", chunk->IsRead() ? "read" : "write", chunk->Index, wchunk.Bytes, ec ? ec.message() : "no progress") );
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
			if( Sync ){//an empty write that syncs.
				_syncThread.Post( [self](){
					if( let syncError = syncBuffers(self->Handle.get(), *self); syncError.Code )
						self->PostExp( up<IFileChunkArg>{}, (uint32)syncError.Code, string{syncError.Message}, true );
					else
						self->ResumeComplete();
				} );
			}
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
