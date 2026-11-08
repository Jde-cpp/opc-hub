#include <liburing.h>
#include <sys/eventfd.h>
#include <boost/asio.hpp>
#include <jde/fwk/io/file.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/thread.h>
#include "../../../io/FileOp.h"

#define let const auto
namespace Jde::IO{
	struct io_uring _ring;
	int _eventFd{ -1 };//signaled by the kernel when a cqe posts; -1 → fall back to polling.
	Ω releaseEventSd()ι->void;
	α Internal::LinuxInit()ε->void{
		//No file io works without the ring - the polling below replaces only the eventfd.  liburing returns -errno.
		let ring = io_uring_queue_init( 256, &_ring, 0 );
		THROW_IF( ring<0, "io_uring is unavailable: {} - file io needs it.  In a container, allow the io_uring syscalls (--security-opt seccomp=unconfined, or a profile that allows io_uring_setup, io_uring_enter and io_uring_register).  On the host, kernel.io_uring_disabled must be 0.", strerror(-ring) );
		_eventFd = ::eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );
		let registered = _eventFd==-1 ? -errno : io_uring_register_eventfd( &_ring, _eventFd );
		if( registered<0 ){
			WARNT( ELogTags::IO, "eventfd registration failed - file io will poll for completions. errno: {}", -registered );
			if( _eventFd!=-1 ){
				::close( _eventFd );
				_eventFd = -1;
			}
		}
		Process::AddFinalizeFunction( [](bool){ releaseEventSd(); } );//after the join:  the descriptor must die before the io_context does.
	}
	using namespace Internal;
	constexpr ELogTags _tags{ ELogTags::IO };

	OpenFile::OpenFile( fs::path path, SL sl )ε:
		_path{ move(path) }, _impl{ mu<Impl>() }{
		auto& fd = _impl->Fd;
		fd = ::open( _path.c_str(), O_RDONLY | O_CLOEXEC );
		if( fd==-1 )
			throw IOException{ _path, (uint32)errno, "open", sl };
		struct stat st;//a throw from here destroys _impl, which closes fd.
		THROW_IFX( ::fstat(fd, &st)==-1, IOException(_path, (uint32)errno, "fstat", sl) );
		if( !S_ISREG(st.st_mode) ){//a FIFO or a directory has no size to read at offsets.
			IOException e{ _path, "is not a regular file", sl };
			e.Error = EIOError::Invalid;
			throw e;
		}
	}
	OpenFile::Impl::~Impl(){
		if( Fd!=-1 ){
			::close( Fd );
			TRACE( "[{}]Closed file handle.", hex(Fd) );
		}
		for( let fd : NameDirs )
			::close( fd );
	}
	α OpenFile::Size( SL sl )Ε->uint{
		struct stat st;
		THROW_IFX( ::fstat(_impl->Fd, &st)==-1, IOException(_path, (uint32)errno, "fstat", sl) );
		return (uint)st.st_size;
	}
	α OpenFile::Read( uint offset, char* buffer, uint size, SL sl )Ε->uint{
		uint y{};
		while( y<size ){
			let read = ::pread( _impl->Fd, buffer+y, size-y, (off_t)(offset+y) );
			if( read==-1 ){
				if( errno==EINTR )
					continue;
				throw IOException{ _path, (uint32)errno, "pread", sl };
			}
			if( !read )
				break;//the end of the file.
			y += (uint)read;
		}
		return y;
	}

	α OpenFile::ForWrite( fs::path path, const WriteOptions& options, SL sl )ε->up<OpenFile>{
		auto impl = mu<Impl>();//closes what this opened, on a throw.
		auto& fd = impl->Fd;
		auto flags = O_WRONLY | O_NONBLOCK | O_CLOEXEC;
		let keepsPrefix = options.Mode==EWriteMode::Truncate && options.Offset;
		if( options.Create && !keepsPrefix )//a file made here has no prefix to keep.
			flags |= O_CREAT;
		if( options.Mode==EWriteMode::Append )
			flags |= O_APPEND;
		//A synced write that may create the file counts the names it adds, so Start can sync their directories as well.
		//O_CREAT alone doesn't say whether it created, so the file is opened as new (O_EXCL), then as existing (no O_CREAT).
		//Either is the plain open: all there is without syncNames, and the last resort with it, for a file deleted between
		//the two or a dangling symlink - the name is then synced whether or not it is new.
		let syncNames = options.Sync && ( flags & O_CREAT );
		enum class EOpen : uint8{ New, Existing, Either };
		auto how = syncNames ? EOpen::New : EOpen::Either;
		uint newNames{};
		for( bool retried{};; ){
			fd = ::open( path.string().c_str(), how==EOpen::New ? flags | O_EXCL : how==EOpen::Existing ? flags & ~O_CREAT : flags, 0666 );
			if( fd!=-1 ){
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
				let parent = path.parent_path();
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
				THROW_IFX( ec, IOException(path, (uint32)ec.value(), "create_directories", sl) );
				continue;//made here, or by another thread a moment ago, which create_directories can't tell from there all along.
			}//a second ENOENT, the parent there all along:  the open's own error stands.
			throw IOException{ path, (uint32)err, "open", sl };
		}
		auto dir = path.parent_path();
		for( uint i=0; i<newNames; ++i, dir = dir.parent_path() ){//the file's directory, then the parent of each directory made for it.
			let dirFd = ::open( dir.empty() ? "." : dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC );
			THROW_IFX( dirFd==-1, IOException(dir, errno, "open", sl) );
			impl->NameDirs.push_back( dirFd );
		}
		if( options.Mode==EWriteMode::Truncate ){//the file ends at Offset, the bytes kept, and the data follows.  One cut for every Offset, 0 included: without it a shorter write leaves the old file's tail in place.
			struct stat st;
			THROW_IFX( ::fstat(fd, &st)==-1, IOException(path, errno, "fstat", sl) );
			CheckPrefix( path, options.Offset, (uint)st.st_size, sl );
			if( (uint)st.st_size>options.Offset )//nothing to cut from a new file, or from a device, which has no length and refuses ftruncate.
				THROW_IFX( ::ftruncate(fd, (off_t)options.Offset)==-1, IOException(path, errno, "ftruncate", sl) );
		}
		TRACE( "[{}]{} {}", hex(fd), options.Mode==EWriteMode::Append ? "appending" : options.Mode==EWriteMode::Truncate ? "truncating" : "writing at", path.string() );
		return up<OpenFile>{ new OpenFile{move(path), move(impl)} };
	}

	Ω prepChunk( up<Chunk>&& chunk )ι->bool{
		auto& op = *chunk->Op;
		struct io_uring_sqe *sqe = io_uring_get_sqe( &_ring );
		if( !sqe ){
			BREAK;
			op.Complete( EBUSY, Ƒ("Could not get file queue:  aio_{} index: {}\n", op.IsRead ? "read" : "write", chunk->Index) );
			return false;
		}
		//no IOSQE_IO_LINK: a link chain spans whatever sqes share a submission batch - including unrelated
		//ops/files - and a short read severs the chain, canceling the rest with -ECANCELED. Ordering isn't
		//needed here: reads and positioned writes use explicit offsets, and an append has only one chunk in flight (Start).
		if( op.IsRead ){
			TRACE( "Preparing read: {}, index: {}, bytes: {}", op.Path.string(), chunk->Index, chunk->Bytes );
			io_uring_prep_read( sqe, op.Native().Fd, op.Data()+chunk->Start, chunk->Bytes, chunk->Offset );
		}
		else{
			TRACE( "Preparing write: {}, index: {}, bytes: {}", op.Path.string(), chunk->Index, chunk->Bytes );
			io_uring_prep_write( sqe, op.Native().Fd, op.Data()+chunk->Start, chunk->Bytes, op.Appends() ? (__u64)-1 : chunk->Offset );//-1: O_APPEND places an append.
		}
		io_uring_sqe_set_data( sqe, chunk.release() );
		return true;
	}
	Ω prepNext( const sp<FileOp>& op )ι->bool{//the next chunk not yet queued:  none while a window's others are still in flight.
		auto index = op->NextChunk();
		if( !index ){
			TRACE( "No more chunks to queue for file IO: {}, size: {}, chunks: {}, inFlight: {}", op->Path.string(), op->Size(), op->ChunksToSend, FileOp::InFlight() );
			return false;
		}
		return prepChunk( mu<Chunk>(op, *index) );
	}

	//Queued when every chunk of a write has completed, so it covers every byte of the op and needs no link to order it.
	//Step 0 is the file's fdatasync; step n fsyncs NameDirs[n-1], each queued when the step before it completes.
	Ω prepSync( const sp<FileOp>& op, uint step=0 )ι->bool{
		struct io_uring_sqe* sqe = io_uring_get_sqe( &_ring );
		if( !sqe ){
			op->Complete( EBUSY, step ? "Could not get file queue:  directory fsync\n" : "Could not get file queue:  fdatasync\n", true );
			return false;
		}
		if( step ){
			DBGT( op->_tags, "[{}]Syncing directory {} of {}.", op->Path.string(), step, op->NameDirs().size() );
			io_uring_prep_fsync( sqe, op->NameDirs()[step-1], 0 );
		}
		else{
			TRACE( "Preparing fdatasync: {}", op->Path.string() );
			io_uring_prep_fsync( sqe, op->Native().Fd, IORING_FSYNC_DATASYNC );
		}
		io_uring_sqe_set_data( sqe, new Chunk{op, Chunk::SyncTag{step}} );
		return true;
	}

	Ω processFinishedChunks( uint size, struct io_uring_cqe** cqe )ι->vector<sp<FileOp>>{
		vector<sp<FileOp>> submitOps;
		vector<sp<FileOp>> completedOps;
		for( uint i=0; i<size; ++i ){
			struct io_uring_cqe* cq = cqe[i];
			//no IORING_CQE_F_MORE handling: the flag is multishot-only (accept/recv/poll) - impossible for plain
			//file read/write sqes - and skipping io_uring_cqe_seen would make peek_batch return the cqe forever.
			let res = cq->res;
			up<Chunk> chunk{ (Chunk*)io_uring_cqe_get_data(cq) };
			io_uring_cqe_seen( &_ring, cq );//releases the slot for kernel reuse - cq must not be read past this point.
			ASSERT( chunk );
			if( !chunk )
				continue;
			auto op = chunk->Op;
			//EINVAL from a directory's fsync is a file system with none, vboxsf say, which has nothing to lose:  the chain
			//goes on, as the file's own fdatasync never does past an error.
			if( res < 0 && !(chunk->Sync && *chunk->Sync && -res==EINVAL) ){
				op->Complete( -res, chunk->Sync ? Ƒ("{} failed: {}\n", *chunk->Sync ? "directory fsync" : "fdatasync", strerror(-res)) : Ƒ("AIO index: {} failed: {}\n", chunk->Index, strerror(-res)), chunk->Sync.has_value() );
				continue;
			}
			if( chunk->Sync ){//before the byte counts below: it moved none, and its res of 0 would read as no progress.
				if( let next = op->Synced(*chunk->Sync); next ){
					if( prepSync(op, *next) )
						submitOps.push_back( move(op) );
				}
				else
					completedOps.push_back( move(op) );
				continue;
			}
			switch( op->Transferred(*chunk, (uint)res) ){
			case ETransfer::NoProgress://EOF or full disk; resubmitting would loop forever.
				op->Complete( EIO, Ƒ("AIO index: {} {} returned 0 with {} bytes remaining.\n", chunk->Index, op->IsRead ? "read" : "write", chunk->Bytes) );
				break;
			case ETransfer::Resubmit:
				if( prepChunk(move(chunk)) )
					submitOps.push_back( move(op) );
				break;
			case ETransfer::Next:
				if( prepNext(op) )
					submitOps.push_back( move(op) );
				break;
			case ETransfer::Sync:
				if( prepSync(op) )
					submitOps.push_back( move(op) );
				break;
			case ETransfer::Done:
				completedOps.push_back( move(op) );
				break;
			}
		}
		for( auto& completed : completedOps )
			completed->Complete();
		return submitOps;
	}

	Ω armCompletionWait( ELogTags tags )ι->void;
	Ω submit( sp<FileOp> op, ELogTags _tags )ι->void{
		TRACE( "Submitting file IO: {}, size: {}, chunks: {}, inFlight: {}, isRead: {}", op->Path.string(), op->Size(), op->ChunksToSend, FileOp::InFlight(), op->IsRead );
		int result = io_uring_submit( &_ring );
		if( result>=0 ){
			if( FileOp::InFlight() )//can be 0: a lone op that failed in prepChunk has completed already.
				armCompletionWait( _tags );
		}
		else{
			CRITICAL( "io_uring_submit failed: {}", strerror(-result) );
			op->Complete( -result, "io_uring_submit failed" );
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
		if( FileOp::InFlight() )
			armCompletionWait( _tags );
	}

	up<boost::asio::posix::stream_descriptor> _eventSd;
	uint64_t _eventCount;
	bool _armed{};//io-strand confined
	Ω releaseEventSd()ι->void{
		if( _eventSd ){
			_eventSd->release();//keeps the eventfd open & registered with the ring.
			_eventSd = nullptr;
			_armed = false;
		}
	}
	struct EventFdShutdown final : IShutdown{
		α Shutdown( bool terminate, SL )ι->void override{
			if( !terminate )//its pending read would keep run() from returning.  A terminate's stop() ends it regardless.
				PostIO( releaseEventSd );
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
			[tags]( const boost::system::error_code& ec, uint ){
				if( ec )
					return;//cancelled at shutdown
				PostIO( [tags](){
					_armed = false;
					drainCompletions( tags );
				} );
			} );
	}
	α Internal::Start( sp<FileOp> op )ι->void{
		//appends (offset -1/O_APPEND): concurrent in-flight chunks can be executed out of order by the
		//kernel (io-wq), permuting the file's contents - only one of its chunks may be in flight at a time.
		//reads and the positioned writes use explicit offsets, so a window of parallel chunks is safe: a
		//write's sync is queued once every chunk has completed, whatever order they finish in.
		PostIO( [op, window = op->Appends() ? std::min<uint>(1, op->ChunksToSend) : op->Window(), tags=_tags ](){
			for( uint i=0; i<window; ++i )
				prepNext( op );
			if( !op->ChunksToSend )//an empty write that syncs.
				prepSync( op );
			submit( move(op), tags );
		} );
	}
}