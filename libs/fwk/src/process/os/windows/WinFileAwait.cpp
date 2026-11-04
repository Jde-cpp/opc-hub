#include <cerrno>
#include <boost/asio.hpp>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/thread.h>
#include <jde/fwk/str.h>
#include "../../../io/FileOp.h"

#define let const auto

namespace Jde::IO{
	using namespace Internal;
	namespace asio = boost::asio;
	constexpr ELogTags _tags{ ELogTags::IO };
	using RandomAccessHandle=asio::windows::random_access_handle;//owns the file HANDLE & registers it with the executor's iocp; closed on the op's terminal path, destroyed when the last chunk handler releases it.
	Ω winPath( const fs::path& path )->string{ return string{"\\\\?\\"}+Str::Replace( fs::absolute(path).string(), '/', '\\' ); }//\\?\ takes no relative path, and no forward slash.
	constexpr DWORD shareAll = FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;

	//The io_context first, so the handle deregisters from a live one however long the OpenFile outlives the executor.
	struct OpenFile::Impl::Asio final{
		Asio( sp<asio::io_context> ioc, HANDLE h )ε:Ioc{ move(ioc) }, Handle{ *Ioc, h }{}//system_error when the iocp refuses h.
		sp<asio::io_context> Ioc;
		RandomAccessHandle Handle;
	};
	OpenFile::OpenFile( fs::path path, SL sl )ε:
		_path{ move(path) }, _impl{ mu<Impl>() }{
		const HANDLE file = ::CreateFile( winPath(_path).c_str(), GENERIC_READ, shareAll, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr );
		const DWORD openError = ::GetLastError();
		_impl->Handle = HandlePtr( WinHandle(file, [&](){ return IOException( _path, openError, "CreateFile", sl ); }) );
		if( ::GetFileType(_impl->Handle.get())!=FILE_TYPE_DISK ){//a pipe or a device has no size to read at offsets.
			IOException e{ _path, "is not a regular file", sl };
			e.Error = EIOError::Invalid;
			throw e;
		}
	}
	α OpenFile::Size( SL sl )Ε->uint{
		LARGE_INTEGER size;
		THROW_IFX( !::GetFileSizeEx(_impl->Handle.get(), &size), IOException(_path, ::GetLastError(), "GetFileSizeEx", sl) );
		return (uint)size.QuadPart;
	}
	α OpenFile::Read( uint offset, char* buffer, uint size, SL sl )Ε->uint{
		//An event of its own, its low-order bit set:  once Shared has put the file on asio's iocp, a read without one queues a
		//packet there for an OVERLAPPED gone by the time asio takes it, and waits on the file, which any read on it signals.
		//The thread's, since it waits on one read at a time, and ReadFile resets it.
		thread_local HandlePtr event;
		if( !event )
			event = ManualResetEvent();
		THROW_IFX( !event, IOException(_path, ::GetLastError(), "CreateEvent", sl) );
		uint y{};
		while( y<size ){//the handle is overlapped, so each read names its offset and is waited for.
			OVERLAPPED at{};
			at.Offset = (DWORD)( offset+y );
			at.OffsetHigh = (DWORD)( (offset+y)>>32 );
			at.hEvent = reinterpret_cast<HANDLE>( reinterpret_cast<uintptr_t>((HANDLE)event.get()) | 1 );
			DWORD read{};
			if( !::ReadFile(_impl->Handle.get(), buffer+y, (DWORD)(size-y), nullptr, &at) && ::GetLastError()!=ERROR_IO_PENDING ){
				if( ::GetLastError()==ERROR_HANDLE_EOF )
					break;
				throw IOException{ _path, ::GetLastError(), "ReadFile", sl };
			}
			if( !::GetOverlappedResult(_impl->Handle.get(), &at, &read, TRUE) ){
				if( ::GetLastError()==ERROR_HANDLE_EOF )
					break;
				throw IOException{ _path, ::GetLastError(), "GetOverlappedResult", sl };
			}
			if( !read )
				break;
			y += read;
		}
		return y;
	}
	//The asio handle the reads of an OpenFile share, made on the first:  a duplicate, so the OpenFile keeps a HANDLE of its own
	//for Size while asio owns the one on the iocp, since a file object registers with an iocp once.
	Ω sharedAsio( const OpenFile& file, SL sl )ε->sp<OpenFile::Impl::Asio>{
		auto& native = file.Native();
		lg _{ native.Mutex };
		if( !native.Shared ){
			auto executor = Executor();
			THROW_IFX( !executor, IOException(file.Path(), "no executor to read on", sl) );
			HANDLE dup;
			THROW_IFX( !::DuplicateHandle(::GetCurrentProcess(), native.Handle.get(), ::GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS), IOException(file.Path(), ::GetLastError(), "DuplicateHandle", sl) );
			try{
				native.Shared = ms<OpenFile::Impl::Asio>( move(executor), dup );
			}
			catch( const boost::system::system_error& e ){
				::CloseHandle( dup );
				throw IOException{ file.Path(), (uint32)e.code().value(), Ƒ("iocp registration failed: {}", e.what()), sl };
			}
		}
		return native.Shared;
	}

	α OpenFile::ForWrite( fs::path path, const WriteOptions& options, SL sl )ε->up<OpenFile>{
		auto impl = mu<Impl>();//its HandlePtrs self-close, on a throw.
		let keepsPrefix = options.Mode==EWriteMode::Truncate && options.Offset;
		//Shares read and write in every mode, as linux locks nothing:  a reader of the file, a live day's file at its first append (a Truncate at 0) among them, would otherwise fail the write or be failed by it.  A file that must change whole for its readers goes to a temp renamed over it.  Not delete, so the file isn't removed or renamed while it is written.
		//Create picks whether a missing file is an error in every mode, matching linux's O_CREAT - except for a Truncate that keeps a prefix, since a file made here would have none to keep.  No disposition truncates: a Truncate sets the end of the file below, whatever its Offset.
		const DWORD creationDisposition = options.Create && !keepsPrefix ? OPEN_ALWAYS : OPEN_EXISTING;
		const HANDLE file = ::CreateFile( winPath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, creationDisposition, FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_ARCHIVE, nullptr );
		const DWORD openError = ::GetLastError();//read before anything else can set it: a failure's code, or ERROR_ALREADY_EXISTS when an open that could create found the file there.
		impl->Handle = HandlePtr( WinHandle(file, [&](){ return IOException( path, openError, "CreateFile", sl ); }) );
		let created = creationDisposition==OPEN_ALWAYS && openError!=ERROR_ALREADY_EXISTS;
		if( options.Sync && created ){//the directory gained the file's name, which Sync covers.  FILE_FLAG_BACKUP_SEMANTICS is what opens a directory, and FlushFileBuffers needs GENERIC_WRITE.
			let parent = path.parent_path();
			const HANDLE dir = ::CreateFile( winPath(parent.empty() ? fs::path{"."} : parent).c_str(), GENERIC_WRITE, shareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr );
			const DWORD dirError = ::GetLastError();
			impl->NameDirs.emplace_back( WinHandle(dir, [&](){ return IOException( parent, dirError, "CreateFile", sl ); }) );
		}
		if( options.Mode==EWriteMode::Truncate && ::GetFileType(impl->Handle.get())==FILE_TYPE_DISK ){//the file ends at Offset, the bytes kept, and the data follows.  One cut for every Offset, 0 included: without it a shorter write leaves the old file's tail in place.  A device has no length to read or set.
			LARGE_INTEGER fileSize;
			THROW_IFX( !::GetFileSizeEx(impl->Handle.get(), &fileSize), IOException(path, GetLastError(), "GetFileSizeEx", sl) );
			let size = (uint)fileSize.QuadPart;
			CheckPrefix( path, options.Offset, size, sl );
			if( size>options.Offset ){//By handle, not SetEndOfFile: an overlapped handle has no file pointer to set it from.
				FILE_END_OF_FILE_INFO end;
				end.EndOfFile.QuadPart = (LONGLONG)options.Offset;
				THROW_IFX( !::SetFileInformationByHandle(impl->Handle.get(), FileEndOfFileInfo, &end, sizeof(end)), IOException(path, GetLastError(), "SetFileInformationByHandle", sl) );
			}
		}
		return up<OpenFile>{ new OpenFile{move(path), move(impl)} };
	}

	Ω start( const sp<RandomAccessHandle>& h, sp<Chunk>&& chunk )ι->void;
	Ω startNext( const sp<RandomAccessHandle>& h, const sp<FileOp>& op )ι->void{//the next chunk not yet started:  none while a window's others are still in flight, or after a failure.
		if( auto index = op->NextChunk(); index )
			start( h, ms<Chunk>(op, *index) );
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
	//The chain, a step at a time:  the file, then each directory Open added a name to.  Runs on _syncThread.
	Ω syncBuffers( HANDLE file, const FileOp& op )ι->SyncError{
		for( optional<uint> step{ 0 }; step; step = op.Synced(*step) ){
			if( *step )
				DBGT( op._tags, "[{}]Syncing directory {} of {}.", op.Path.string(), *step, op.NameDirs().size() );
			if( !::FlushFileBuffers(*step ? op.NameDirs()[*step-1].get() : file) )
				return { ::GetLastError(), *step ? "FlushFileBuffers failed for the directory\n" : "FlushFileBuffers failed\n" };
		}
		return {};
	}

	Ω finish( const sp<RandomAccessHandle>& h, const sp<FileOp>& op, SyncError syncError )ι->void{
		if( !op->IsRead ){//a read's handle is its OpenFile's, with other reads on it; a write's is this op's own.
			boost::system::error_code ec;
			h->close( ec );//before the resume posts, so the coroutine never finds its own handle still open.  Nothing races it: every chunk has completed, so no initiation is queued.
			if( ec )
				WARN( "[{}]close failed: {}", op->Path.string(), ec.message() );
		}
		if( syncError.Code )
			op->Complete( (uint32)syncError.Code, string{syncError.Message}, true );
		else
			op->Complete();
	}

	Ω onChunk( const sp<RandomAccessHandle>& h, const sp<Chunk>& chunk, const boost::system::error_code& ec, uint bytes )ι->void{
		auto& op = chunk->Op;
		switch( op->Transferred(*chunk, bytes) ){
		case ETransfer::Resubmit://a hard error resurfaces on the resubmit.
			start( h, sp<Chunk>{chunk} );
			break;
		case ETransfer::Next:
			startNext( h, op );
			break;
		case ETransfer::Sync:
			_syncThread.Post( [h, op](){ finish( h, op, syncBuffers(h->native_handle(), *op) ); } );
			break;
		case ETransfer::Done:
			finish( h, op, {} );
			break;
		case ETransfer::NoProgress://error or zero progress - resume with the exception; the close cancels the op's other in-flight chunks (they land here with operation_aborted & no-op: no chunk starts after the failure & the handle is closed).  A read's handle stays open for its OpenFile's other reads, and this op's in-flight chunks land as silent completions (Complete, won already).
			if( !op->IsRead )
				PostIO( [h](){ boost::system::error_code closeEc; h->close(closeEc); } );//via the strand - a close may not race an initiation.
			op->Complete( ec ? (uint32)ec.value() : (uint32)ERROR_IO_DEVICE, Ƒ("{} index: {} failed with {} bytes remaining: {}\n", op->IsRead ? "read" : "write", chunk->Index, chunk->Bytes, ec ? ec.message() : "no progress") );
			break;
		}
	}

	Ω start( const sp<RandomAccessHandle>& h, sp<Chunk>&& chunk )ι->void{
		PostIO( [h, chunk=move(chunk)](){//initiations are strand-serialized - asio handles aren't safe for concurrent calls on the same object.
			if( !h->is_open() )//an error path closed the handle after this initiation was queued.
				return;
			auto& op = chunk->Op;
			TRACE( "({}){} chunk {}: offset {}, {} bytes.", op->Path.string(), op->IsRead ? "Reading" : "Writing", chunk->Index, chunk->Offset, chunk->Bytes );
			auto handler = [h, chunk]( const boost::system::error_code& ec, size_t bytes ){ onChunk( h, chunk, ec, (uint)bytes ); };
			if( op->IsRead )
				h->async_read_some_at( chunk->Offset, asio::buffer(op->Data()+chunk->Start, chunk->Bytes), move(handler) );
			else
				h->async_write_some_at( chunk->Offset, asio::buffer(op->Data()+chunk->Start, chunk->Bytes), move(handler) );
		} );
	}

	α Internal::Start( sp<FileOp> op )ι->void{
		TRACE( "[{}] chunks = {}", op->Path.string(), op->ChunksToSend );
		if( !op->ChunksToSend ){//an empty write that syncs:  the chain alone, on its thread.
			_syncThread.Post( [op](){
				if( let syncError = syncBuffers(op->Native().Handle.get(), *op); syncError.Code )
					op->Complete( (uint32)syncError.Code, string{syncError.Message}, true );
				else
					op->Complete();
			} );
			return;
		}
		auto executor = Executor();
		if( !executor ){//finalizing - the io could never run; mirror Post's drop semantics.
			WARN( "[{}]Start after executor teardown - dropping file io.", op->Path.string() );
			return;
		}
		sp<RandomAccessHandle> handle;
		if( op->IsRead ){//the OpenFile's asio handle, shared by its reads and kept alive by the aliasing pointer through the chunks' copies.
			try{
				auto asio = sharedAsio( op->File(), op->_sl );
				handle = sp<RandomAccessHandle>{ asio, &asio->Handle };
			}
			catch( IOException& e ){
				op->Complete( e.Move() );
				return;
			}
		}
		else{//the write's own, registered here and closed by finish before the resume.
			HANDLE raw = op->Native().Handle.release();
			try{
				handle = ms<RandomAccessHandle>( *executor, raw );
			}
			catch( const boost::system::system_error& e ){
				::CloseHandle( raw );
				op->Complete( (uint32)e.code().value(), Ƒ("iocp registration failed: {}", e.what()) );
				return;
			}
		}
		//explicit offsets make parallel chunks order-independent for reads & writes both - the append base is fixed in Open.
		for( uint i=0, window=op->Window(); i<window; ++i )
			startNext( handle, op );
	}
}