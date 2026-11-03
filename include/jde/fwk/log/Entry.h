#pragma once
#ifndef ENTRY_H
#define ENTRY_H
#include <fmt/args.h>
#include <jde/fwk/log/logTags.h>
#include <jde/fwk/utils/paramPack.h>

#define Φ Γ auto
#define let const auto
#define FormatString const fmt::format_string<Args const&...>
#define ARGS const Args&

// namespace Jde{
// 	struct Exception;
// }
//SourceLocation() below is only declared here - a declaration may return an incomplete type - so this
//header no longer drags <spdlog/common.h> into <jde/fwk.h> and from there into every TU in the repo.
//Entry.cpp includes it for the definition; SpdLog.h has it for the call.
namespace spdlog{ struct source_loc; }

namespace Jde::Logging{
	Φ GenerateId( sv text )ι->StringMd5;//md5 of the text - the id every log entry, file and function is keyed by.

	//A file or function name as an Entry carries it: a static string from source_location (a view, no copy) or owned text
	//off the wire, plus its id hashed at most once.  One type for both names - File()/Function(), FileString()/
	//FunctionString() and FileId()/FunctionId() used to be three copied pairs over two variants and two memos.
	struct SourceName{
		SourceName()ι=default;
		SourceName( sv text )ι:_text{ text }{}
		SourceName( string&& text )ι:_text{ move(text) }{}
		SourceName( StringMd5 id )ι:_id{ id }{}//the wire form that carries only the hash.
		α View()Ι->sv{ return _text.index()==0 ? std::get<sv>(_text) : std::get<string>(_text); }
		α Str()ι->string&{ if( _text.index()==0 ) _text = string{ std::get<sv>(_text) }; return std::get<string>(_text); }//owned: a view is copied in once, then served in place.
		α Id()Ι->StringMd5{ if( !_id ) _id = GenerateId( View() ); return *_id; }
	private:
		variant<sv,string> _text;
		mutable optional<StringMd5> _id;
	};

#ifndef __cpp_lib_stacktrace
	//A frame's file, line and function, as boost's source_file, source_line and name give them, from one symbolizer for the process.
	//Boost's libbacktrace state is per thread, and a thread's first use parses the binary's DWARF under glibc's loader lock - 1.5 s
	//in a debug build, one thread at a time - so an executor thread's first open62541 log line waited out every other thread's.
	struct FrameLocation final{ string File; uint32_t Line{}; string Function; };
	Φ Locate( const boost::stacktrace::frame& frame )ι->FrameLocation;
#endif
	struct Γ Entry final{
		template<class... Args> Entry( SL sl, ELogLevel l, ELogTags tags, string&& m, ARGS... args )ι;
		template<class... Args> Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, string&& m, ARGS... args )ι;
#ifdef __cpp_lib_stacktrace
		template<class... Args> Entry( const std::stacktrace_entry& sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι;
		template<class... Args> Entry( const std::stacktrace_entry& sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι;
#else
		template<class... Args> Entry( const boost::stacktrace::frame& sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι;
		template<class... Args> Entry( const boost::stacktrace::frame& sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι;
#endif
		Entry( SL sl, ELogLevel l, ELogTags tags, string&& m, vector<string> args={} )ι;
		Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, string&& m, vector<string> args )ι;
		Entry( ELogLevel l, ELogTags tags, uint32_t line, TimePoint time, Jde::UserPK userId, uuid messageId, uuid fileId, uuid functionId, vector<string>&& args )ι;
		Entry( ELogLevel l, ELogTags tags, uint32_t line, TimePoint time, Jde::UserPK userId, string&& text, string&& file, string&& function, vector<string>&& args )ι;

		Ω GenerateId( sv text )ι->StringMd5{ return Logging::GenerateId( text ); }

		α Id()Ι->StringMd5{ if( !_id )_id = GenerateId(Text); return *_id; }
		α File()Ι->sv{ return _file.View(); }
		α FileString()ι->string&{ return _file.Str(); }
		α FileId()Ι->StringMd5{ return _file.Id(); }
		α Function()Ι->sv{ return _function.View(); }
		α FunctionString()ι->string&{ return _function.Str(); }
		α FunctionId()Ι->StringMd5{ return _function.Id(); }
		α Message()Ι->string;
		α SourceLocation()Ι->spdlog::source_loc;
		string Text; //template string with {} for args
		vector<string> Arguments;
		ELogLevel Level;
		ELogTags Tags;
		uint32_t Line;
		TimePoint Time;
		Jde::UserPK UserPK;
	private:
		SourceName _file;
		SourceName _function;
		mutable string _message;
		mutable optional<StringMd5> _id;
	};

	template<class... Args>
	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, string&& m, ARGS... args )ι:
		Entry{ sl, l, tags, Jde::UserPK{}, FWD(m), FWD(args)... }
	{}

	template<class... Args>
	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, string&& m, ARGS... args )ι:
		Entry{ sl, l, tags, userPK, move(m), vector<string>{} }{
		ParamPack::Append( Arguments, args... );
	}

#ifdef __cpp_lib_stacktrace
	template<class... Args>
	Entry::Entry( const std::stacktrace_entry& sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι:
		Entry{ sl, l, tags, {}, FWD(m), FWD(args)... }
	{}

	template<class... Args>
	Entry::Entry( const std::stacktrace_entry& sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι:
		Text{ m.get().data(), m.get().size() },
		Level{ l },
		Tags{ tags },
		Line{ sl.source_line() },
		Time{ Clock::now() },
		UserPK{ userPK },
		_file{ sl.source_file() },
		_function{ sl.description() },
		_message{ fmt::vformat(m, fmt::make_format_args(FWD(args)...)) }{
		ParamPack::Append( Arguments, FWD(args)... );
	}
#else
	template<class... Args>
	Entry::Entry( const boost::stacktrace::frame& sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι:
		Entry{ sl, l, tags, {}, FWD(m), FWD(args)... }
	{}
	template<class... Args>
	Entry::Entry( const boost::stacktrace::frame& sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι:
		Text{ m.get().data(), m.get().size() },
		Level{ l },
		Tags{ tags },
		Time{ Clock::now() },
		UserPK{ userPK },
		_message{ fmt::vformat(m, fmt::make_format_args(FWD(args)...)) }{
		auto at = Locate( sl );
		Line = at.Line;
		_file = SourceName{ move(at.File) };
		_function = SourceName{ move(at.Function) };
		ParamPack::Append( Arguments, FWD(args)... );
	}
#endif

}
#undef ARGS
#undef FormatString
#undef Φ
#undef let
#endif