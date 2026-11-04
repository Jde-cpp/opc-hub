#pragma once
#ifndef FILE_AWAIT_H
#define FILE_AWAIT_H
#include <jde/fwk/co/AnyAwait.h>

#define Φ Γ auto

namespace Jde::IO{
	Φ Init()ε->void;

	Φ ChunkByteSize()ι->uint32;
	Φ WindowSize()ι->uint8;//chunks of one transfer in flight at once:  /workers/io/threads.

	//Truncate replaces the file - required whenever data is the whole file, otherwise a shorter write leaves the previous version's tail behind.  At writes over what is there and leaves the rest of the file alone.
	//Every mode shares the file with its readers, on windows as on linux, so a read by path under a Truncate can return bytes of both versions:  a file read while it is replaced whole goes to a temp, which IO::Replace puts over the name.
	enum class EWriteMode : uint8{ Append, Truncate, At };
	struct WriteOptions{
		bool Create{};//makes the file when it isn't there, and on linux the directories on its path.
		EWriteMode Mode{ EWriteMode::Append };
		uint Offset{};//Truncate: the bytes kept, which the data follows - the file has to hold them already, whatever Create says, or the write fails.  At: where the data goes.  Append takes none: one that is set fails the write, since it is an At that lost its Mode.
		bool Sync{};//the file's data is on the device before the awaiter resumes:  fdatasync on linux, FlushFileBuffers on windows.  So is a name the write added, the file's or a parent directory's:  each directory that gained one is synced after the data.  A sync that fails throws an IOException with Written set.
		ELogTags Tags{ ELogTags::IO };
	};
	struct ReadOptions{
		uint Offset{};
		optional<uint> Size;//at most this many bytes - fewer, or none, when the file ends first.
		ELogTags Tags{ ELogTags::IO };
	};

	//A file open to read at any offset, shared by the reads of it:  they read the file it opened whatever is renamed over
	//its path after, where a read by path would open the new file.  Linux O_RDONLY|O_CLOEXEC, blocking (a FIFO's open waits
	//for its writer, as an ifstream's does); Windows GENERIC_READ sharing read, write and delete, overlapped, with one asio
	//handle for every read of it.  Or open to write, by ForWrite:  a WriteAwait's, and the whole of how a file is opened for one.
	struct Γ OpenFile final : noncopyable{
		OpenFile( fs::path path, SRCE )ε;//IOException, Error NotFound for a file that isn't there.
		//Open for the write the options describe:  made when Create says so, and on linux the directories on its path; cut to
		//Offset for a Truncate, which fails past the end of the file; the names a synced write adds counted, each directory
		//that gained one held for the sync.  Shares read and write, and not delete.  IOException, as the read open.
		Ω ForWrite( fs::path path, const WriteOptions& options, SRCE )ε->up<OpenFile>;
		~OpenFile();
		α Path()Ι->const fs::path&{ return _path; }
		α Size( SRCE )Ε->uint;//now:  the file may have grown since the open, and a read is clamped to it.
		//Up to size bytes at offset, read now on the calling thread, fewer at the end of the file and none past it:  the
		//synchronous counterpart of a ReadAwait over the file, for a caller that can't await, such as one on an executor thread.
		α Read( uint offset, char* buffer, uint size, SRCE )Ε->uint;
		struct Impl;//the platform's handle, an fd or a HANDLE with the asio handle the reads share, and a write's name directories:  defined with the ops in libs/fwk/src/io/FileOp.h, so no platform type is here.
		α Native()Ι->Impl&;
	private:
		OpenFile( fs::path path, up<Impl> impl )ι;//ForWrite's.
		const fs::path _path;
		up<Impl> _impl;
	};

	namespace Internal{ struct ReadOp; struct WriteOp; }//the ops behind the awaitables, libs/fwk/src/io/FileOp.h:  the platform files run them, and nothing a caller sees.
	//Any family:  a coroutine of any return type co_awaits one, and an open that fails completes it in await_ready, with no
	//suspend.  Movable until it suspends, as the family is - its op then resumes it by address, so a suspended one stays put.
	struct Γ ReadAwait final : AnyAwait<string>{
		ReadAwait( fs::path path, ReadOptions options={}, SRCE )ι;
		//Over an open file:  its bytes at Offset, whatever is at its path now.
		ReadAwait( sp<OpenFile> file, ReadOptions options={}, SRCE )ι;
		α await_ready()ι->bool override;//opens and sizes the buffer:  a range with no bytes in it completes here too, with an empty string.
		α await_resume()ε->string;
	private:
		α Suspend()ι->void override;
		sp<Internal::ReadOp> _op;
	};
	struct Γ WriteAwait final : AnyVoidAwait{
		WriteAwait( fs::path path, variant<string,vector<byte>> data, WriteOptions options={}, SRCE )ι;
		α await_ready()ι->bool override;//opens:  a failure, the options' or the file's, completes here.
		α await_resume()ε->void;
	private:
		α Suspend()ι->void override;
		sp<Internal::WriteOp> _op;
	};
}
#undef Φ
#endif