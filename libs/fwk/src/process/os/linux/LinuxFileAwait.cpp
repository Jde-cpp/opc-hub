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
		struct SyncTag{};
		LinuxChunk( sp<FileIOArg> arg, SyncTag )ι://the fdatasync that follows a write's last chunk - it moves no bytes.
			IFileChunkArg{ arg, arg->ChunksToSend },
			StartIndex{},
			EndIndex{},
			Bytes{},
			IsSync{ true }
		{}
#undef StartIndex
		uint StartIndex;
		uint EndIndex;
		uint Bytes;
		bool IsSync{};
	};


	α FileIOArg::Open( bool create )ε->void{
		auto flags = O_NONBLOCK | O_CLOEXEC | ( IsRead ? O_RDONLY : O_WRONLY );
		if( !IsRead ){
			if( create )
				flags |= O_CREAT;
			if( Mode==EWriteMode::Append )
				flags |= O_APPEND;
			else if( Mode==EWriteMode::Truncate && !Offset )
				flags |= O_TRUNC;//O_TRUNC, not a plain overwrite: chunks write from offset 0, so without it a shorter write leaves the old file's tail in place.
		}
		for( bool retried = false;; retried = true ){
			Handle = ::open( Path.string().c_str(), flags, 0666 );
			if( Handle!=-1 )
				break;
			let err = errno;
			if( !retried && !IsRead && err==ENOENT ){//parent dir may not exist - create it & retry once.
				std::error_code ec;
				fs::create_directories( Path.parent_path(), ec );
				THROW_IFX( ec, IOException(Path, (uint32)ec.value(), "create_directories", _sl) );//copy, not move: keep Path for later logging on this object.
				INFO( "Created dir {}", Path.parent_path().string() );
				continue;
			}
			throw IOException{ Path, (uint32)err, "open", _sl };
		}
		if( IsRead ){
			struct stat st;
			THROW_IFX( ::fstat( Handle, &st )==-1, IOException(Path, errno, "fstat", _sl) );
			TRACE( "[{}]Opened file: {}, size: {}", hex(Handle), Path.string(), st.st_size );
			let fileSize = (uint)st.st_size;
			let size = fileSize>Offset ? std::min( Limit.value_or(fileSize), fileSize-Offset ) : 0;//a range that starts at or past the end reads nothing.
			std::visit( [size](auto&& b){b.resize(size);}, Buffer );
		}
		else{
			if( Mode==EWriteMode::Truncate && Offset )//keeps the first Offset bytes - O_TRUNC keeps none.
				THROW_IFX( ::ftruncate(Handle, (off_t)Offset)==-1, IOException(Path, errno, "ftruncate", _sl) );
			TRACE( "[{}]{} {}", hex(Handle), Mode==EWriteMode::Append ? "appending" : Mode==EWriteMode::Truncate ? "truncating" : "writing at", Path.string() );
		}
	}
	FileIOArg::~FileIOArg(){
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
		//needed here: reads use explicit offsets and writes allow only one chunk in flight (Send).

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

	//Queued when a write's last chunk has completed, so it covers every byte of the op and needs no link to order it.
	Ω prepSync( const sp<FileIOArg>& op )ι->bool{
		struct io_uring_sqe* sqe = io_uring_get_sqe( &_ring );
		if( !sqe ){
			markFinished( op );
			op->PostExp( {}, EBUSY, "Could not get file queue:  fdatasync\n" );
			return false;
		}
		TRACE( "Preparing fdatasync: {}", op->Path.string() );
		io_uring_prep_fsync( sqe, op->Handle, IORING_FSYNC_DATASYNC );
		io_uring_sqe_set_data( sqe, new LinuxChunk{op, LinuxChunk::SyncTag{}} );
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
			if( res < 0 ){
				auto op = chunk->FileArg();
				//Message first, as at the EBUSY site above.  PostExp takes up<IFileChunkArg>&&, so passing a up<LinuxChunk>
				//converts - and that conversion moves, emptying `chunk`.  Argument order is unspecified, and clang empties it
				//before evaluating the format, so reading chunk->Index inline dereferenced null: every failed read/write
				//(EISDIR, EIO, ENOSPC) crashed the process instead of reporting.
				auto message = chunk->IsSync ? Ƒ( "fdatasync failed: {}\n", strerror(-res) ) : Ƒ( "AIO index: {} failed: {}\n", chunk->Index, strerror(-res) );
				markFinished( op );
				op->PostExp( move(chunk), -res, move(message) );
				continue;
			}
			if( chunk->IsSync ){//before the byte counts below: it moved none, and its res of 0 would read as no progress.
				completedOps.push_back( chunk->FileArg() );
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
		//kernel (io-wq), permuting the file's contents - only one write chunk may be in flight at a time.
		//The other writes keep that window too: a sync has to follow the last of them.
		//reads use explicit offsets, so a window of parallel chunks is safe.
		PostIO( [self, initialSendTotal = IsRead ? std::min<uint>(ChunksToSend, threadSize) : 1u, tags=_tags ](){
			for( uint i=0; i<initialSendTotal && i<self->ChunksToSend; ++i )
				addNextChunkToQueue( self );
			if( !self->ChunksToSend )//an empty write that syncs.
				prepSync( self );
			submit( move(self), tags );
		} );
	}
}