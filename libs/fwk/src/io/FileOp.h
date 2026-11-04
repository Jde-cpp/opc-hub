#pragma once
#ifdef _MSC_VER
	#include <jde/fwk/process/os/windows/WindowsHandle.h>
#endif
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/process/execution.h>

namespace Jde::IO{
	//The platform's handles on an OpenFile, which its Native() returns:  the public header names the type and nothing more.
	//NameDirs:  a synced write's, the directories ForWrite added a name to, the file's own first - each is synced after the data.
	struct OpenFile::Impl final{
#ifdef _MSC_VER
		HandlePtr Handle;//self-closing.  A write's, Start releases into the asio handle, which then owns the close.
		struct Asio;//the random_access_handle the reads share, over a duplicate of the HANDLE:  made on the first read, owned here.
		sp<Asio> Shared; std::mutex Mutex;
		vector<HandlePtr> NameDirs;//windows only ever adds the file's name.
#else
		~Impl();//closes Fd and the NameDirs.
		int Fd{ -1 };//-1 is "no handle"; 0 is a *valid* descriptor.
		vector<int> NameDirs;
#endif
	};
}

//The ops behind ReadAwait and WriteAwait, and the chunks they move in:  shared by FileAwait.cpp and the platform files, and nothing a caller sees.
namespace Jde::IO::Internal{
#ifdef _MSC_VER
	using HFile=HandlePtr;
#else
	using HFile=int;
	α LinuxInit()ε->void;//the io_uring and its eventfd, for IO::Init.
#endif

	struct FileOp;
	//A chunk of an op's bytes in flight, or a step of its sync chain:  a value the platform files carry through their
	//completions, linux as the sqe's data and windows in the handler.
	struct Chunk{
		Chunk( sp<FileOp> op, uint index )ι;//the index'th ChunkByteSize() of the op's bytes, up to their end.
		struct SyncTag{ uint Step; };
		Chunk( sp<FileOp> op, SyncTag sync )ι:Op{ move(op) }, Sync{ sync.Step }{}//a step of the chain after the last byte:  it moves none.
		sp<FileOp> Op;
		uint Index{};
		uint Start{};//into the op's bytes.  A partial transfer takes the bytes moved from Bytes and adds them here and to Offset.
		uint Bytes{};//left to move.
		uint Offset{};//in the file.  Linux ignores an append's, O_APPEND placing each chunk; windows' Open set the op's Offset to the end it found.
		optional<uint> Sync;//the step:  0 the file's data, n NameDirs[n-1].
	};
	//What a chunk's completion calls for, from FileOp::Transferred.
	enum class ETransfer : uint8{ Resubmit, Next, Sync, Done, NoProgress };

	//What a read and a write share, the transfer:  the handle, the bytes and the offset they go at, the chunks and their
	//decisions, and the terminal the platform files call, which each kind of op completes for its own awaitable.
	struct FileOp : noncopyable{
		FileOp( bool isRead, fs::path path, uint offset, ELogTags tags, SL sl )ι;
		virtual ~FileOp()=default;//the file closes its own handles.
		β Open()ε->void=0;//the file, and a read's buffer:  on the awaiting thread, before the suspend.
		α File()Ι->OpenFile&{ return *_file; }//after Open.
		α Native()Ι->OpenFile::Impl&{ return _file->Native(); }
		α NameDirs()Ι->std::span<const HFile>{ return Native().NameDirs; }
		α Data()ι->char*{ return _bytes.data(); }
		α Size()Ι->uint{ return _bytes.size(); }
		β Appends()Ι->bool{ return false; }//the kernel places each chunk at the end:  no offset, and one chunk in flight at a time.
		β Syncs()Ι->bool{ return false; }//after the last byte, the file's data goes to the device, then each of NameDirs.
		//The decisions, made once for both platforms.
		α NextChunk()ι->optional<uint>;//the index of the next chunk not yet handed out, in order:  none past the last, or after a failure.
		α Transferred( Chunk& chunk, uint bytes )ι->ETransfer;//bytes of the chunk moved.  Resubmit:  the chunk, advanced past them.  NoProgress:  none, which a resubmit would repeat.  Else the chunk is done:  Next while chunks remain, Sync for a write that syncs once the last has, Done.
		α Synced( uint step )Ι->optional<uint>;//the step after one of the chain, none after the last.
		α Window()Ι->uint;//chunks in flight at once:  WindowSize, or every chunk when there are fewer.
		//The terminal, from any thread.  The first call wins Finished and posts the resume, with the result or the failure, and
		//no chunk starts after a failure; a later call is nothing, which a chunk that lands after another's failure needs.
		//written:  the failure is a sync step's, after the last byte - IOException::Written.
		α Complete( up<Exception> error={} )ι->void;
		α Complete( uint32 code, string&& msg, bool written=false )ι->void;
		Ω InFlight()ι->uint;//ops Send has taken and Complete has not:  what linux arms its completion wait on.

		std::atomic<bool> Finished{};//won by the first Complete.
		atomic<uint> ChunksCompleted;
		uint ChunksToSend{};
		uint Offset{};//the file offset of the first byte.  Appends: linux leaves it 0, O_APPEND placing the bytes, and windows' Open sets it to the EOF it finds.
		const bool IsRead;
		fs::path Path;
		SL _sl;
		ELogTags _tags;
	protected:
		sp<OpenFile> _file;//Open's:  a read's own, or the caller's it borrows; a write's from ForWrite.
		α Send( sp<FileOp> self )ι->void;//from TypedOp::Send, the awaiter in place; self is this op, for Start and the chunks to hold.  Counts the chunks, completes a write with none and no sync, and gives the rest to the platform's Start.
		β PostResume( up<Exception> error )ι->void=0;//from the Complete that won:  posts the awaiter's resume with the op's result, or the error.
		std::span<char> _bytes;//the buffer a read fills, the data a write sends.
	private:
		atomic<uint> _nextChunk{};//the cursor NextChunk hands out from.
	};
	//The platform's start:  the first Window of chunks, or the sync chain of a write with none.  Linux submits to the ring,
	//windows to the handle on asio's iocp; each carries the chunks on from there, through the op's decisions.
	α Start( sp<FileOp> op )ι->void;

	//The awaitable, by address:  stored before the io starts, taken once by the Complete that won, so no lock.  Any family,
	//so it holds its own result and continuation and does not move once suspended - the address holds until the resume,
	//which is the last use of it.
	template<class TAwait>
	struct TypedOp : FileOp{
		using FileOp::FileOp;
		Ω Send( sp<TypedOp> self, TAwait& a )ι->void{ auto& op = *self; op._await = &a; op.FileOp::Send( move(self) ); }
	protected:
		α Take()ι->TAwait*{ auto a = std::exchange( _await, nullptr ); ASSERT( a ); return a; }
	private:
		TAwait* _await{};
	};

	struct ReadOp final : TypedOp<ReadAwait>{
		ReadOp( fs::path path, ReadOptions options, SL sl )ι;
		ReadOp( sp<OpenFile> file, ReadOptions options, SL sl )ι;//over the caller's file:  the op borrows it, and closes nothing.
		α Open()ε->void override;//an OpenFile of the path, unless the caller's; then the buffer.
		α SizeBuffer( uint fileSize )ε->void;//Buffer takes the range's share of a file that long.
		α PostResume( up<Exception> error )ι->void override;
		string Buffer;
		const optional<uint> Limit;//the most bytes to take from Offset.
	};
	struct WriteOp final : TypedOp<WriteAwait>{
		WriteOp( fs::path path, variant<string,vector<byte>> data, WriteOptions options, SL sl )ι;
		α Open()ε->void override;//the options checked, an Append taking no Offset; then OpenFile::ForWrite, and on windows an append's base offset, the end it found.
		α Appends()Ι->bool override{ return Options.Mode==EWriteMode::Append; }
		α Syncs()Ι->bool override{ return Options.Sync; }
		α PostResume( up<Exception> error )ι->void override;
		variant<string,vector<byte>> Data;
		const WriteOptions Options;
	};
	//For ForWrite's Truncate:  throws unless a file that long holds the offset bytes it keeps - cutting a shorter file to the
	//offset grows it with zeros, which would then pass for the kept bytes.
	α CheckPrefix( const fs::path& path, uint offset, uint fileSize, SL sl )ε->void;
}