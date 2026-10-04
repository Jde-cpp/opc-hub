#pragma once
#ifndef FILECO_H
#define FILECO_H
#include <queue>
#include <jde/fwk/co/Task.h>
#ifdef _MSC_VER
	#include <jde/fwk/process/os/windows/WindowsHandle.h>
	using HFile=Jde::HandlePtr;
#else
	using HFile=int;
#endif
#include <jde/fwk/co/Await.h>

#define Φ Γ auto

namespace Jde::IO{
	Φ Init()ι->void;
#ifndef _MSC_VER
	α LinuxInit()ι->void;
#endif

	Φ ChunkByteSize()ι->uint32;
	Φ ThreadSize()ι->uint8;

	//Truncate replaces the file - required whenever data is the whole file, otherwise a shorter write leaves the previous version's tail behind.  At writes over what is there and leaves the rest of the file alone.
	enum class EWriteMode : uint8{ Append, Truncate, At };
	struct WriteOptions{
		bool Create{};
		EWriteMode Mode{ EWriteMode::Append };
		uint Offset{};//Truncate: the bytes kept, which the data follows.  At: where the data goes.  Append ignores it.
		bool Sync{};//the file's data is on the device before the awaiter resumes:  fdatasync on linux, FlushFileBuffers on windows.
		ELogTags Tags{ ELogTags::IO };
	};
	struct ReadOptions{
		uint Offset{};
		optional<uint> Size;//at most this many bytes - fewer, or none, when the file ends first.
		bool Cache{};//whole-file reads only - the cache is keyed by path.
	};

	struct FileIOArg;
	struct IFileChunkArg{
		IFileChunkArg( sp<FileIOArg>& arg, uint index ):
			Index{index},
			_fileIOArg{arg}
		{}
		virtual ~IFileChunkArg()=default;
		β Handle()Ι->HFile&;
		β Process()ι->void{};
		β FileArg()Ι->sp<FileIOArg>{ return _fileIOArg; }
		α IsRead()Ι->bool;

		uint Index;
	private:
		sp<FileIOArg> _fileIOArg;
	};

	struct FileIOArg final : std::enable_shared_from_this<FileIOArg>, noncopyable{
		using HCo=variant<StringAwait::Handle,VoidAwait::Handle>;
		FileIOArg( fs::path path, bool vec, SRCE )ι;
		FileIOArg( fs::path path, variant<string,vector<byte>> data, ELogTags tags, SRCE )ι;
		~FileIOArg();
		α Open( bool create )ε->void;
		α Send( HCo h )ι->void;
		α Data()ι->char*{ return visit( [](auto&& x){return (char*)x.data();}, Buffer ); }
		α Size()Ι{ return visit( [](auto&& x){return x.size();}, Buffer ); }
		α PostExp( up<IFileChunkArg>&& chunk, uint32 code, string&& msg )ι->void;
		α ResumeExp( uint32 code, string&& msg )ι->void;
		α ResumeExp( uint32 code, string&& m, lg& chunkLock )ι->void;

		α CoHandle()ι->HCo{ lg _{_coHandleMutex}; auto h = _coHandle; if( IsRead ) _coHandle = StringAwait::Handle{}; else _coHandle = VoidAwait::Handle{}; return h; }
		α ResumeComplete()ι->void;

		variant<string,vector<byte>> Buffer;
		std::queue<up<IFileChunkArg>> Chunks; std::mutex ChunkMutex;
		std::atomic<bool> Finished{};//set on the op's first terminal transition (all chunks completed, or an error resume) - guards one-shot bookkeeping such as the platform request count.
		atomic<uint> ChunksCompleted;
		uint ChunksToSend;
		uint Offset{};//the file offset of Buffer's first byte.  Appends: linux leaves it 0, O_APPEND placing the bytes, and windows' Open sets it to the EOF it finds.
		optional<uint> Limit;//reads: the most bytes to take from Offset.
		EWriteMode Mode{ EWriteMode::Append };
		bool Sync{};
#ifdef _MSC_VER
		HFile Handle{};//owning HandlePtr - null is the "no handle" sentinel and it self-closes; Send releases it into the asio handle, which then owns the close.
#else
		HFile Handle{ -1 };//raw fd - -1 is "no handle"; 0 is a *valid* descriptor and must not be conflated with unopened.
#endif
		bool IsRead;
		fs::path Path;
		SL _sl;
		ELogTags _tags;
		HCo _coHandle; mutex _coHandleMutex;
	};

	struct Γ IFileAwait{
		IFileAwait( fs::path&& path, bool vec, SL sl )ι:_arg{ ms<FileIOArg>(move(path), vec, sl) }{}
		IFileAwait( fs::path&& path, variant<string,vector<byte>>&& data, ELogTags tags, SL sl )ι:_arg{ ms<FileIOArg>(move(path), move(data), tags, sl) }{}
		up<Exception> ExceptionPtr;
		sp<FileIOArg> _arg;
	};
	struct Γ ReadAwait final : IFileAwait, StringAwait, noncopyable{
		ReadAwait( fs::path path, bool cache=false, SRCE )ι:ReadAwait{ move(path), ReadOptions{.Cache=cache}, sl }{}
		ReadAwait( fs::path path, ReadOptions options, SRCE )ι:IFileAwait{ move(path), false, sl }, StringAwait{ sl }, _cache{ options.Cache && !options.Offset && !options.Size }{
			_arg->Offset = options.Offset;
			_arg->Limit = options.Size;
		}
		α Suspend()ι->void override;
		α await_ready()ι->bool override;
		α await_resume()ε->string override;
	private:
		bool _cache;
		bool _fromCache{};//ready path taken - the buffer is the result, even when empty; there is no promise to fall back on.
	};
	struct Γ WriteAwait final : IFileAwait, VoidAwait, noncopyable{
		WriteAwait( fs::path path, variant<string,vector<byte>> data, bool create=false, EWriteMode mode=EWriteMode::Append, ELogTags tags=ELogTags::IO, SRCE )ι:
			WriteAwait{ move(path), move(data), WriteOptions{.Create=create, .Mode=mode, .Tags=tags}, sl }{}
		WriteAwait( fs::path path, variant<string,vector<byte>> data, WriteOptions options, SRCE )ι:
			IFileAwait{ move(path), move(data), options.Tags, sl }, VoidAwait{ sl }, _create{ options.Create }{
			_arg->Mode = options.Mode;
			_arg->Offset = options.Mode==EWriteMode::Append ? 0 : options.Offset;
			_arg->Sync = options.Sync;
		}
		α Suspend()ι->void override;
		α await_ready()ι->bool override;
		α await_resume()ε->void override;
	private:
		bool _create;
	};
}
#undef Φ
#endif