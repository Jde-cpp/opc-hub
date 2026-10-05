#include <liburing.h>
#include <sys/eventfd.h>
#include <boost/asio.hpp>
#include <jde/fwk/io/Cache.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/thread.h>

#define let const auto
namespace Jde{
	struct io_uring _ring;
	int _eventFd{ -1 };//signaled by the kernel when a cqe posts; -1 → fall back to polling.
	α IO::LinuxInit()ι->void{
		io_uring_queue_init( 256, &_ring, 0 );
		_eventFd = ::eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );
		if( _eventFd==-1 || io_uring_register_eventfd(&_ring, _eventFd)<0 ){
			WARNT( ELogTags::IO, "eventfd registration failed - file io will poll for completions. errno: {}", errno );
			if( _eventFd!=-1 ){
				::close( _eventFd );
				_eventFd = -1;
			}
		}
	}
}
namespace Jde::IO{
	constexpr ELogTags _tags{ ELogTags::IO };
	atomic<uint> _requestCount{};

	struct LinuxChunk final : IFileChunkArg{
		LinuxChunk( sp<FileIOArg> arg, uint index )ι:
			IFileChunkArg{ arg, index },
			StartIndex{ Index*ChunkByteSize() },
			EndIndex{ std::min(StartIndex+ChunkByteSize(), FileArg()->Size()) },
			Bytes{ EndIndex-StartIndex }
		{}
		struct SyncTag{ uint Step; };//0: the file's fdatasync.  n: the fsync of NameDirs[n-1].
		LinuxChunk( sp<FileIOArg> arg, SyncTag sync )ι://the syncs that follow a write's last chunk - they move no bytes.
			IFileChunkArg{ arg, arg->ChunksToSend },
			StartIndex{},
			EndIndex{},
			Bytes{},
			IsSync{ true },
			SyncStep{ sync.Step }
		{}
#undef StartIndex
		uint StartIndex;
		uint EndIndex;
		uint Bytes;
		bool IsSync{};
		uint SyncStep{};
	};


	α FileIOArg::Open( bool create )ε->void{
		auto flags = O_NONBLOCK | O_CLOEXEC | ( IsRead ? O_RDONLY : O_WRONLY );
		let keepsPrefix = Mode==EWriteMode::Truncate && Offset;
		if( !IsRead ){
			if( create && !keepsPrefix )//a file made here has no prefix to keep.
				flags |= O_CREAT;
			if( Mode==EWriteMode::Append )
				flags |= O_APPEND;
		}
		//A synced write that may create the file counts the names it adds, so Send can sync their directories as well.
		//O_CREAT alone doesn't say whether it created, so the file is opened as new (O_EXCL), then as existing (no O_CREAT).
		//Either is the plain open: all there is without syncNames, and the last resort with it, for a file deleted between
		//the two or a dangling symlink - the name is then synced whether or not it is new.
		let syncNames = Sync && ( flags & O_CREAT );
		enum class EOpen : uint8{ New, Existing, Either };
		auto how = syncNames ? EOpen::New : EOpen::Either;
		uint newNames{};
		for( bool retried{};; ){
			Handle = ::open( Path.string().c_str(), how==EOpen::New ? flags | O_EXCL : how==EOpen::Existing ? flags & ~O_CREAT : flags, 0666 );
			if( Handle!=-1 ){
				if( syncNames && how!=EOpen::Existing )
					++newNames;
				break;
			}
			let err = errno;
			if( how==EOpen::New && err==EEXIST ){
				how = EOpen::Existing;
				continue;
			}
			if( how==EOpen::Existing && err==ENOENT ){
				how = EOpen::Either;
				continue;
			}
			if( !retried && (flags & O_CREAT) && err==ENOENT ){//the parent dir may not exist - an open that makes the file makes it too, and retries once.
				retried = true;
				let parent = Path.parent_path();
				std::error_code ec;
				if( syncNames ){
					for( auto dir = parent; !dir.empty() && !fs::exists(dir, ec); dir = dir.parent_path() )
						++newNames;
				}
				let created = !parent.empty() && fs::create_directories( parent, ec );
				if( created )
					INFO( "Created dir {}", parent.string() );
				if( syncNames && !created && !ec )//another thread made it since this open failed for want of it, so its name is new all the same.
					newNames = std::max<uint>( newNames, 1 );
				THROW_IFX( ec, IOException(Path, (uint32)ec.value(), "create_directories", _sl) );//copy, not move: keep Path for later logging on this object.
				continue;//made here, or by another thread a moment ago, which create_directories can't tell from there all along.
			}//a second ENOENT, the parent there all along:  the open's own error stands.
			throw IOException{ Path, (uint32)err, "open", _sl };
		}
		auto dir = Path.parent_path();
		for( uint i=0; i<newNames; ++i, dir = dir.parent_path() ){//the file's directory, then the parent of each directory made for it.
			let fd = ::open( dir.empty() ? "." : dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC );
			THROW_IFX( fd==-1, IOException(dir, errno, "open", _sl) );
			NameDirs.push_back( fd );
		}
		if( IsRead ){
			struct stat st;
			THROW_IFX( ::fstat( Handle, &st )==-1, IOException(Path, errno, "fstat", _sl) );
			TRACE( "[{}]Opened file: {}, size: {}", hex(Handle), Path.string(), st.st_size );
			SizeBuffer( (uint)st.st_size );
		}
		else{
			if( Mode==EWriteMode::Truncate ){//the file ends at Offset, the bytes kept, and the data follows.  One cut for every Offset, 0 included: without it a shorter write leaves the old file's tail in place.
				struct stat st;
				THROW_IFX( ::fstat(Handle, &st)==-1, IOException(Path, errno, "fstat", _sl) );
				CheckPrefix( (uint)st.st_size );
				if( (uint)st.st_size>Offset )//nothing to cut from a new file, or from a device, which has no length and refuses ftruncate.
					THROW_IFX( ::ftruncate(Handle, (off_t)Offset)==-1, IOException(Path, errno, "ftruncate", _sl) );
			}
			TRACE( "[{}]{} {}", hex(Handle), Mode==EWriteMode::Append ? "appending" : Mode==EWriteMode::Truncate ? "truncating" : "writing at", Path.string() );
		}
	}
	FileIOArg::~FileIOArg(){
		for( let fd : NameDirs )
			::close( fd );
		if( Handle>=0 ){//fd -1 is invalid
			::close( Handle );
			TRACE( "[{}]Closed file handle for {}", hex(Handle), Path.string() );
			Handle = -1;
		}
	}

	Ω markFinished( const sp<FileIOArg>& op )ι->void{//balance Send's ++_requestCount exactly once per op - a multi-chunk read can error on more than one in-flight chunk.
		if( !op->Finished.exchange(true) )
			--_requestCount;
	}

	Ω prepChunk( up<IFileChunkArg>&& chunk, const sp<FileIOArg>& op )ι->bool{
		let index = chunk->Index; let isRead = chunk->IsRead();
		LinuxChunk& lchunk = dynamic_cast<LinuxChunk&>( *chunk );
		struct io_uring_sqe *sqe = io_uring_get_sqe( &_ring );
		if( !sqe ){
			BREAK;
			auto message = Ƒ( "Could not get file queue:  aio_{} index: {}\n", isRead ? "read" : "write", index );
			markFinished( op );
			op->PostExp( move(chunk), EBUSY, move(message) );
			return false;
		}
		//no IOSQE_IO_LINK: a link chain spans whatever sqes share a submission batch - including unrelated
		//ops/files - and a short read severs the chain, canceling the rest with -ECANCELED. Ordering isn't
		//needed here: reads and positioned writes use explicit offsets, and an append has only one chunk in flight (Send).

		if( isRead ){
			TRACE( "Preparing read: {}, index: {}, bytes: {}", op->Path.string(), lchunk.Index, lchunk.Bytes );
			io_uring_prep_read( sqe, op->Handle, op->Data()+lchunk.StartIndex, lchunk.Bytes, op->Offset+lchunk.StartIndex );
		}
		else{
			TRACE( "Preparing write: {}, index: {}, bytes: {}", op->Path.string(), lchunk.Index, lchunk.Bytes );
			const __u64 offset = op->Mode==EWriteMode::Append ? (__u64)-1 : op->Offset+lchunk.StartIndex;//-1: O_APPEND places an append.
			io_uring_prep_write( sqe, op->Handle, op->Data()+lchunk.StartIndex, lchunk.Bytes, offset );
		}
		io_uring_sqe_set_data( sqe, chunk.release() );
		return true;
	}

	//Queued when every chunk of a write has completed, so it covers every byte of the op and needs no link to order it.
	//Step 0 is the file's fdatasync; step n fsyncs NameDirs[n-1], each queued when the step before it completes.
	Ω prepSync( const sp<FileIOArg>& op, uint step=0 )ι->bool{
		struct io_uring_sqe* sqe = io_uring_get_sqe( &_ring );
		if( !sqe ){
			markFinished( op );
			op->PostExp( {}, EBUSY, step ? "Could not get file queue:  directory fsync\n" : "Could not get file queue:  fdatasync\n", true );
			return false;
		}
		if( step ){
			DBGT( op->_tags, "[{}]Syncing directory {} of {}.", op->Path.string(), step, op->NameDirs.size() );
			io_uring_prep_fsync( sqe, op->NameDirs[step-1], 0 );
		}
		else{
			TRACE( "Preparing fdatasync: {}", op->Path.string() );
			io_uring_prep_fsync( sqe, op->Handle, IORING_FSYNC_DATASYNC );
		}
		io_uring_sqe_set_data( sqe, new LinuxChunk{op, LinuxChunk::SyncTag{step}} );
		return true;
	}

	Ω addNextChunkToQueue( sp<FileIOArg> op )ι->bool;
	Ω processFinishedChunks( uint size, struct io_uring_cqe** cqe )ι->vector<sp<FileIOArg>>{
		vector<sp<FileIOArg>> submitOps;
		vector<sp<FileIOArg>> completedOps;
		for( uint i=0; i<size; ++i ){
			struct io_uring_cqe* cq = cqe[i];
			//no IORING_CQE_F_MORE handling: the flag is multishot-only (accept/recv/poll) - impossible for plain
			//file read/write sqes - and skipping io_uring_cqe_seen would make peek_batch return the cqe forever.
			let res = cq->res;
			up<LinuxChunk> chunk{ (LinuxChunk*)io_uring_cqe_get_data(cq) };
			io_uring_cqe_seen( &_ring, cq );//releases the slot for kernel reuse - cq must not be read past this point.
			ASSERT( chunk );
			if( !chunk )
				continue;
			//EINVAL from a directory's fsync is a file system with none, vboxsf say, which has nothing to lose:  the chain
			//goes on, as the file's own fdatasync never does past an error.
			if( res < 0 && !(chunk->IsSync && chunk->SyncStep && -res==EINVAL) ){
				auto op = chunk->FileArg();
				//Message first, as at the EBUSY site above.  PostExp takes up<IFileChunkArg>&&, so passing a up<LinuxChunk>
				//converts - and that conversion moves, emptying `chunk`.  Argument order is unspecified, and clang empties it
				//before evaluating the format, so reading chunk->Index inline dereferenced null: every failed read/write
				//(EISDIR, EIO, ENOSPC) crashed the process instead of reporting.
				let isSync = chunk->IsSync;
				auto message = isSync ? Ƒ( "{} failed: {}\n", chunk->SyncStep ? "directory fsync" : "fdatasync", strerror(-res) ) : Ƒ( "AIO index: {} failed: {}\n", chunk->Index, strerror(-res) );
				markFinished( op );
				op->PostExp( move(chunk), -res, move(message), isSync );
				continue;
			}
			if( chunk->IsSync ){//before the byte counts below: it moved none, and its res of 0 would read as no progress.
				auto op = chunk->FileArg();
				if( chunk->SyncStep<op->NameDirs.size() ){
					if( prepSync(op, chunk->SyncStep+1) )
						submitOps.push_back( move(op) );
				}
				else
					completedOps.push_back( move(op) );
				continue;
			}
			if( (uint)res < chunk->Bytes ){//partial read/write - resubmit the remainder.
				auto op = chunk->FileArg();
				if( res==0 ){//no progress - EOF or full disk; resubmitting would loop forever.
					auto message = Ƒ( "AIO index: {} {} returned 0 with {} bytes remaining.\n", chunk->Index, chunk->IsRead() ? "read" : "write", chunk->Bytes );//before the move, same as above.
					markFinished( op );
					op->PostExp( move(chunk), EIO, move(message) );
					continue;
				}
				TRACE( "Partial {}: {}, index: {}, completed: {} of {} - resubmitting remainder.", chunk->IsRead() ? "read" : "write", op->Path.string(), chunk->Index, res, chunk->Bytes );
				chunk->StartIndex += res;
				chunk->Bytes -= res;
				if( prepChunk(move(chunk), op) )
					submitOps.push_back( op );
				continue;
			}
			auto op = chunk->FileArg();
			if( op->ChunksToSend>++op->ChunksCompleted ){
				if( addNextChunkToQueue(op) )
					submitOps.push_back(op);
			}
			else if( op->Sync ){
				if( prepSync(op) )
					submitOps.push_back( op );
			}
			else
				completedOps.push_back(op);
		}
		for( auto& completed : completedOps ){
			markFinished( completed );
			completed->ResumeComplete();
		}
		return submitOps;
	}

	Ω armCompletionWait( ELogTags tags )ι->void;
	Ω submit( sp<FileIOArg> op, ELogTags _tags )ι->void{
		TRACE( "Submitting file IO: {}, size: {}, chunks: {}, requestCount: {}, isRead: {}", op->Path.string(), op->Size(), op->ChunksToSend, _requestCount.load(), op->IsRead );
		int result = io_uring_submit( &_ring );
		if( result>=0 ){
			if( _requestCount )//can be 0: a lone op that failed in prepChunk was already finished.
				armCompletionWait( _tags );
		}
		else{
			CRITICAL( "io_uring_submit failed: {}", strerror(-result) );
			markFinished( op );
			op->ResumeExp( -result, "io_uring_submit failed" );
		}
	}

	Ω drainCompletions( ELogTags _tags )ι->void{
		array<struct io_uring_cqe*, 16> cqe;
		for(;;){
			let size = io_uring_peek_batch_cqe( &_ring, cqe.data(), cqe.size() );
			if( !size )
				break;
			auto submitOps = processFinishedChunks( size, cqe.data() );
			for( auto& op : submitOps )
				submit( move(op), _tags );
		}
		if( _requestCount )
			armCompletionWait( _tags );
	}

	up<boost::asio::posix::stream_descriptor> _eventSd;
	uint64_t _eventCount;
	bool _armed{};//io-strand confined
	struct EventFdShutdown final : IShutdown{//descriptor must die before the io_context does.
		α Shutdown( bool /*terminate*/, SL )ι->void override{
			if( _eventSd ){
				_eventSd->release();//keeps the eventfd open & registered with the ring in case the executor restarts.
				_eventSd = nullptr;
				_armed = false;
			}
		}
	};
	EventFdShutdown _eventSdShutdown;

	Ω armCompletionWait( ELogTags tags )ι->void{
		if( _eventFd==-1 ){//no eventfd - fall back to polling.
			PostIO( [tags](){ drainCompletions(tags); } );
			return;
		}
		if( _armed )
			return;
		if( !_eventSd ){
			_eventSd = mu<boost::asio::posix::stream_descriptor>( *Executor(), _eventFd );
			Execution::AddShutdown( &_eventSdShutdown );
		}
		_armed = true;
		boost::asio::async_read( *_eventSd, boost::asio::buffer(&_eventCount, sizeof(_eventCount)),
			[tags]( const boost::system::error_code& ec, size_t ){
				if( ec )
					return;//cancelled at shutdown
				PostIO( [tags](){
					_armed = false;
					drainCompletions( tags );
				} );
			} );
	}
	Ω addNextChunkToQueue( sp<FileIOArg> op )ι->bool{
		up<IFileChunkArg> chunk;
		{
			lg l{ op->ChunkMutex };
			if( !op->Chunks.size() ){
				TRACE( "No more chunks to queue for file IO: {}, size: {}, chunks: {}, requestCount: {}", op->Path.string(), op->Size(), op->ChunksToSend, _requestCount.load() );
				return false;
			}
			chunk = move( op->Chunks.front() );
			op->Chunks.pop();
		}
		return prepChunk( move(chunk), op );
	}
	uint fileIndex{};
	α FileIOArg::Send( HCo h )ι->void{
		_coHandle = h;
		let threadSize = ThreadSize();
		let totalBytes = Size();
		let chunkByteSize = ChunkByteSize();
		auto self = shared_from_this();
		{
			//lg l{ ChunkMutex };
			ChunksToSend = (totalBytes+chunkByteSize-1)/chunkByteSize; //ceil( totalBytes / chunkByteSize )
			for( uint i=0; i*chunkByteSize<totalBytes; ++i )
				Chunks.emplace( mu<LinuxChunk>(self, i) );
		}
		if( ChunksToSend==0 && !Sync ){//empty file - no completions will arrive; resume immediately.
			TRACE( "[{}]Empty file - resuming without io.", Path.string() );
			ResumeComplete();
			return;
		}
		++_requestCount;
		//appends (offset -1/O_APPEND): concurrent in-flight chunks can be executed out of order by the
		//kernel (io-wq), permuting the file's contents - only one of its chunks may be in flight at a time.
		//reads and the positioned writes use explicit offsets, so a window of parallel chunks is safe: a
		//write's sync is queued once every chunk has completed, whatever order they finish in.
		PostIO( [self, initialSendTotal = Mode==EWriteMode::Append ? 1u : std::min<uint>(ChunksToSend, threadSize), tags=_tags ](){
			for( uint i=0; i<initialSendTotal && i<self->ChunksToSend; ++i )
				addNextChunkToQueue( self );
			if( !self->ChunksToSend )//an empty write that syncs.
				prepSync( self );
			submit( move(self), tags );
		} );
	}
}