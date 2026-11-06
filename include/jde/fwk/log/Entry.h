#pragma once
#ifndef ENTRY_H
#define ENTRY_H
#include <fmt/args.h>
#include <jde/fwk/log/logTags.h>
#include <jde/fwk/log/Format.h>

#define Φ Γ auto
#define let const auto
#define FormatString const Jde::Logging::Format<Args const&...>
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
	//GenerateId of a source_location's file or function name, cached by the name's address:  it is static storage, so one
	//hash per call site rather than per line.  Process::FreeLibrary forgets them all, since an unloaded library's addresses
	//can come back holding other names.
	Φ SourceId( const char* name )ι->StringMd5;
	Φ ForgetSourceIds()ι->void;
	//Each argument as a stored entry keeps it:  formatted with its field's spec, otherwise as fmt's {}.
	Φ ToArguments( const FormatView& m, fmt::format_args args )ι->vector<string>;

	//A name as an Entry carries it - its template, file and function: a view of static storage (a source_location's names,
	//a Format's literal) or owned text, off the wire or stripped of specs, plus its id hashed at most once.  A view of a
	//library's storage dangles once Process::FreeLibrary unloads it, as an entry a MemoryLog keeps from it would.
	struct SourceName{
		SourceName()ι=default;
		explicit SourceName( const char* name )ι:_text{ sv{name} }, _static{ name }{}//a source_location's file_name() or function_name().
		SourceName( sv staticText, StringMd5 id )ι:_text{ staticText }, _id{ id }{}//a Format's literal.
		SourceName( string&& text, optional<StringMd5> id={} )ι:_text{ move(text) }, _id{ id }{}
		SourceName( StringMd5 id )ι:_id{ id }{}//the wire form that carries only the hash.
		α View()Ι->sv{ return _text.index()==0 ? std::get<sv>(_text) : std::get<string>(_text); }
		α Str()ι->string&{ if( _text.index()==0 ) _text = string{ std::get<sv>(_text) }; return std::get<string>(_text); }//owned: a view is copied in once, then served in place.
		α Id()Ι->StringMd5{ if( !_id ) _id = _static ? SourceId( _static ) : GenerateId( View() ); return *_id; }
	private:
		variant<sv,string> _text;
		mutable optional<StringMd5> _id;
		const char* _static{};//kept through Str(), which leaves the static name where it was.
	};

#ifndef __cpp_lib_stacktrace
	//A frame's file, line and function, as boost's source_file, source_line and name give them, from one symbolizer for the process.
	//Boost's libbacktrace state is per thread, and a thread's first use parses the binary's DWARF under glibc's loader lock - 1.5 s
	//in a debug build, one thread at a time - so an executor thread's first open62541 log line waited out every other thread's.
	struct FrameLocation final{ string File; uint32_t Line{}; string Function; };
	Φ Locate( const boost::stacktrace::frame& frame )ι->FrameLocation;
#endif
	struct Γ Entry final{
		template<class... Args> Entry( SL sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι;
		template<class... Args> Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι;
#ifdef __cpp_lib_stacktrace
		template<class... Args> Entry( const std::stacktrace_entry& sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι;
		template<class... Args> Entry( const std::stacktrace_entry& sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι;
#else
		template<class... Args> Entry( const boost::stacktrace::frame& sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι;
		template<class... Args> Entry( const boost::stacktrace::frame& sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι;
#endif
		Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, SourceName&& text, vector<string> args )ι;
		//What Emit builds:  the template stored as Format left it, and each argument with its field's spec (ToArguments).
		Entry( SL sl, ELogLevel l, ELogTags tags, const FormatView& m, fmt::format_args args )ι;
		Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, const FormatView& m, fmt::format_args args )ι;
		//id:  the template's, when the caller has it - an Exception's Format.
		Entry( SL sl, ELogLevel l, ELogTags tags, string&& m, vector<string> args={}, optional<StringMd5> id={} )ι;
		Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, string&& m, vector<string> args={}, optional<StringMd5> id={} )ι;
		Entry( ELogLevel l, ELogTags tags, uint32_t line, TimePoint time, Jde::UserPK userId, uuid messageId, uuid fileId, uuid functionId, vector<string>&& args )ι;
		Entry( ELogLevel l, ELogTags tags, uint32_t line, TimePoint time, Jde::UserPK userId, string&& text, string&& file, string&& function, vector<string>&& args )ι;

		Ω GenerateId( sv text )ι->StringMd5{ return Logging::GenerateId( text ); }

		α Text()Ι->sv{ return _text.View(); }//the template, with a plain {} for each argument.
		α TextString()ι->string&{ return _text.Str(); }
		α Id()Ι->StringMd5{ return _text.Id(); }
		α File()Ι->sv{ return _file.View(); }
		α FileString()ι->string&{ return _file.Str(); }
		α FileId()Ι->StringMd5{ return _file.Id(); }
		α Function()Ι->sv{ return _function.View(); }
		α FunctionString()ι->string&{ return _function.Str(); }
		α FunctionId()Ι->StringMd5{ return _function.Id(); }
		α Message()Ι->string;
		α SourceLocation()Ι->spdlog::source_loc;
		vector<string> Arguments;
		ELogLevel Level;
		ELogTags Tags;
		uint32_t Line;
		TimePoint Time;
		Jde::UserPK UserPK;
	private:
		SourceName _text;
		SourceName _file;
		SourceName _function;
		mutable string _message;
	};
	//A line as each logger gets it (ILogger::Write), for that call only.  Emit's carries the call's format and erased arguments
	//and builds its Entry the first time a logger asks;  the rest wrap an Entry already built - an exception's, a stack
	//frame's, or one forwarded from another instance, with its origin.
	struct Γ Record final{
		Record( ELogLevel level, ELogTags tags, SL sl, const FormatView& m, fmt::format_args args )ι:
			Level{ level }, Tags{ tags }, _sl{ sl }, _format{ &m }, _args{ args }{}
		Record( const Logging::Entry& entry, uint32 appPK=0, uint32 instancePK=0 )ι:
			Level{ entry.Level }, Tags{ entry.Tags }, AppPK{ appPK }, InstancePK{ instancePK }, _entry{ &entry }{}
		α Entry()Ι->const Logging::Entry&;
		α Format()Ι->const FormatView*{ return _format; }//Emit's format, null for an Entry's record.
		α Args()Ι->fmt::format_args{ return _args; }
		α Source()Ι->SL{ return _sl; }
		ELogLevel Level;
		ELogTags Tags;
		uint32 AppPK{};//a forwarded entry's origin:  0 for this process's own lines.
		uint32 InstancePK{};
	private:
		SL _sl;
		const FormatView* _format{};
		fmt::format_args _args;
		mutable const Logging::Entry* _entry{};
		mutable optional<Logging::Entry> _built;
	};

	//A Format's template as an Entry keeps it:  a view of the literal, or, with a spec, the literal stripped of it.
	Ξ TemplateName( const FormatView& m )ι->SourceName{ return m.Specs ? SourceName{ m.Template(), optional<StringMd5>{m.Id} } : SourceName{ m.Text, m.Id }; }

	template<class... Args>
	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι:
		Entry{ sl, l, tags, Jde::UserPK{}, m.View(), fmt::make_format_args(args...) }
	{}

	template<class... Args>
	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι:
		Entry{ sl, l, tags, userPK, m.View(), fmt::make_format_args(args...) }
	{}

#ifdef __cpp_lib_stacktrace
	template<class... Args>
	Entry::Entry( const std::stacktrace_entry& sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι:
		Entry{ sl, l, tags, {}, FWD(m), FWD(args)... }
	{}

	template<class... Args>
	Entry::Entry( const std::stacktrace_entry& sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι:
		Arguments{ ToArguments(m.View(), fmt::make_format_args(args...)) },
		Level{ l },
		Tags{ tags },
		Line{ sl.source_line() },
		Time{ Clock::now() },
		UserPK{ userPK },
		_text{ TemplateName(m.View()) },
		_file{ sl.source_file() },
		_function{ sl.description() },
		_message{ fmt::vformat(m.Fmt, fmt::make_format_args(FWD(args)...)) }
	{}
#else
	template<class... Args>
	Entry::Entry( const boost::stacktrace::frame& sl, ELogLevel l, ELogTags tags, FormatString&& m, ARGS... args )ι:
		Entry{ sl, l, tags, {}, FWD(m), FWD(args)... }
	{}
	template<class... Args>
	Entry::Entry( const boost::stacktrace::frame& sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, FormatString&& m, ARGS... args )ι:
		Arguments{ ToArguments(m.View(), fmt::make_format_args(args...)) },
		Level{ l },
		Tags{ tags },
		Time{ Clock::now() },
		UserPK{ userPK },
		_text{ TemplateName(m.View()) },
		_message{ fmt::vformat(m.Fmt, fmt::make_format_args(FWD(args)...)) }{
		auto at = Locate( sl );
		Line = at.Line;
		_file = SourceName{ move(at.File) };
		_function = SourceName{ move(at.Function) };
	}
#endif

}
#undef ARGS
#undef FormatString
#undef Φ
#undef let
#endif