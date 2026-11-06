#include <jde/fwk/log/Entry.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/fwk/crypto/digest.h>
#include <spdlog/common.h>	//Entry.h only forward-declares source_loc; SourceLocation() is defined here.

#define let const auto

namespace Jde::Logging{
	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, string&& m, vector<Argument> args, optional<StringMd5> id )ι:
		Entry( sl, l, tags, {}, move(m), move(args), id )
	{}

	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, string&& m, vector<Argument> args, optional<StringMd5> id )ι:
		Entry( sl, l, tags, userPK, SourceName{move(m), id}, move(args) )
	{}
	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, SourceName&& text, vector<Argument> args )ι:
		Arguments{ move(args) },
		Level{ l },
		Tags{ tags },
		Line{ sl.line() },
		Time{ Clock::now() },
		UserPK{ userPK },
		_text{ move(text) },
		_file{ sl.file_name() },//static storage - copying into strings costs 2 allocations per log call.
		_function{ sl.function_name() }
	{}
	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, const FormatView& m, fmt::format_args args )ι:
		Entry( sl, l, tags, Jde::UserPK{}, m, args )
	{}
	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, const FormatView& m, fmt::format_args args )ι:
		Entry( sl, l, tags, userPK, TemplateName(m), ToArguments(m, args) )
	{}
	Entry::Entry( ELogLevel l, ELogTags tags, uint32_t line, TimePoint time, Jde::UserPK userId, uuid messageId, uuid fileId, uuid functionId, vector<Argument>&& args )ι:
		Arguments{ move(args) },
		Level{ l },
		Tags{ tags },
		Line{ line },
		Time{ time },
		UserPK{ userId },
		_text{ messageId },
		_file{ fileId },
		_function{ functionId }
	{}
	Entry::Entry( ELogLevel l, ELogTags tags, uint32_t line, TimePoint time, Jde::UserPK userId, string&& text, string&& file, string&& function, vector<Argument>&& args )ι:
		Entry( l, tags, line, time, userId, SourceName{move(text)}, SourceName{move(file)}, SourceName{move(function)}, move(args) )
	{}
	Entry::Entry( ELogLevel l, ELogTags tags, uint32_t line, TimePoint time, Jde::UserPK userId, SourceName&& text, SourceName&& file, SourceName&& function, vector<Argument>&& args )ι:
		Arguments{ move(args) },
		Level{ l },
		Tags{ tags },
		Line{ line },
		Time{ time },
		UserPK{ userId },
		_text{ move(text) },
		_file{ move(file) },
		_function{ move(function) }
	{}

	α GenerateId( sv text )ι->StringMd5{ return Crypto::CalcMd5(text); }

	Ω sourceIds()ι->concurrent_flat_map<const char*,StringMd5>&{ static concurrent_flat_map<const char*,StringMd5> ids; return ids; }
	//What an argument fmt refuses at run time, a null char pointer, is stored as in its place.
	Ω formatError( const fmt::format_error& e )ι->string{ return string{"<format error: "}+e.what()+'>'; }
	//An argument as text:  with its field's spec, then without it if the value can't take the spec at run time, then, if fmt
	//still refuses the value (a null char pointer), the error in its place.
	Ω formatArgument( sv field, fmt::format_args one )ι->string{
		try{
			return fmt::vformat( fmt::string_view{field.data(), field.size()}, one );
		}
		catch( const fmt::format_error& e ){
			if( field=="{}" )
				return formatError( e );
		}
		try{
			return fmt::vformat( "{}", one );
		}
		catch( const fmt::format_error& e ){
			return formatError( e );
		}
	}
	//A plain {} field's argument, as fmt holds it:  int8_t and uint8_t arrive as int and unsigned, and a 128-bit integer, a
	//long double, a char and a custom type are text.  A float is the double that renders as it did - 0.1f as 0.1, not
	//0.10000000149011612 - or its text where no double does:  fmt writes a float in exponent form from 1e7, a double from 1e16.
	Ω toArgument( const fmt::basic_format_arg<fmt::format_context>& arg )ι->Argument{
		return arg.visit( [&arg]<class T>( const T& value )->Argument{
			if constexpr( std::is_same_v<T,bool> || std::is_same_v<T,double> )
				return value;
			else if constexpr( std::is_integral_v<T> && !std::is_same_v<T,char> && sizeof(T)<=8 ){
				if constexpr( std::is_signed_v<T> )
					return int64_t{ value };
				else
					return uint64_t{ value };
			}
			else if constexpr( std::is_same_v<T,float> ){
				auto text = fmt::format( "{}", value );
				double y{};
				std::from_chars( text.data(), text.data()+text.size(), y );
				if( Ƒ("{}", y)==text )
					return y;
				return text;
			}
			else
				return formatArgument( "{}", fmt::format_args{&arg, 1} );
		} );
	}

	α Record::Entry()Ι->const Logging::Entry&{
		if( !_entry )
			_entry = &_built.emplace( _sl, Level, Tags, *_format, _args );
		return *_entry;
	}

	α Entry::SourceLocation()Ι->spdlog::source_loc{
		//NUL-terminated either way: a source_location literal, or the owned string's buffer.  A default name's view is null, which
		//spdlog would strlen whenever the line is set, since its source flags test only the line.
		auto name = []( const SourceName& n )->const char*{ let view = n.View(); return view.data() ? view.data() : ""; };
		return spdlog::source_loc{ name(_file), (int)Line, name(_function) };
	}
	α Entry::Message()Ι->string{
		if( _message.size() )
			return _message;
		try{
			_message = Arguments.size()==0 ? string{ Text() } : Vformat( Text(), Arguments );
		}
		catch( const runtime_error& e ){
			let args = Str::Join( Arguments | std::views::transform(ArgText), ", ", true );
			CRITICALT( Tags, "Bad Format: {}, args: '{}', what: '{}'", Text(), args, e.what() );
			_message = Ƒ( "{}, args: '{}'", Text(), args );
		}
		return _message;
	}
}
namespace Jde{
α Logging::SourceId( const char* name )ι->StringMd5{
	optional<StringMd5> id;
	sourceIds().cvisit( name, [&id]( let& x ){ id = x.second; } );
	if( !id ){
		id = GenerateId( name );
		sourceIds().emplace( name, *id );
	}
	return *id;
}
α Logging::ForgetSourceIds()ι->void{ sourceIds().clear(); }
α Logging::ToArguments( const FormatView& m, fmt::format_args args )ι->vector<Argument>{
	vector<sv> fields;//each field as written, "{:x}", only when some field has a spec.
	if( m.Specs )
		Internal::ForEachField( m.Text, []( sv ){}, [&fields]( sv field, sv ){ fields.push_back( field ); } );
	vector<Argument> y;
	for( int i=0;; ++i ){
		const auto arg = args.get( i );
		if( !arg )
			break;
		if( i<64 && ((m.Specs >> i) & 1) && i<(int)fields.size() )
			y.emplace_back( formatArgument(fields[i], fmt::format_args{&arg, 1}) );
		else
			y.push_back( toArgument(arg) );
	}
	return y;
}
α Logging::ArgText( const Argument& arg )ι->string{
	return std::visit( []( const auto& value ){ return fmt::format( "{}", value ); }, arg );
}
α Logging::Vformat( sv text, const vector<Argument>& args )ε->string{
	fmt::dynamic_format_arg_store<fmt::format_context> store;
	for( let& arg : args )
		std::visit( [&store]( const auto& value ){ store.push_back( value ); }, arg );
	return fmt::vformat( text, store );
}
#ifndef __cpp_lib_stacktrace
α Logging::Locate( const boost::stacktrace::frame& frame )ι->FrameLocation{
#ifdef BOOST_STACKTRACE_USE_BACKTRACE
	FrameLocation y;
	if( !frame )
		return y;
	let pc = reinterpret_cast<uintptr_t>( frame.address() );
	string symbol;
	{
		static std::mutex mutex;
		lg _{ mutex };
		static ::backtrace_state* state = ::backtrace_create_state( nullptr, 0, [](void*, const char*, int){}, nullptr );//not threaded:  the mutex serializes every call.
		if( state ){
			//Every inlined level calls back, innermost first, and the last one stands - as boost's own callback.
			::backtrace_pcinfo( state, pc, []( void* data, uintptr_t, const char* file, int line, const char* function )->int{
				auto& y = *static_cast<FrameLocation*>( data );
				if( file )
					y.File = file;
				if( function )
					y.Function = function;
				y.Line = (uint32_t)line;
				return 0;
			}, [](void*, const char*, int){}, &y );
			if( y.Function.empty() )
				::backtrace_syminfo( state, pc, []( void* data, uintptr_t, const char* name, uintptr_t, uintptr_t ){
					if( name )
						*static_cast<string*>( data ) = name;
				}, [](void*, const char*, int){}, &symbol );
		}
	}
	if( ::Dl_info dli; ::dladdr(frame.address(), &dli) && dli.dli_sname )//boost's name() prefers the dynamic symbol.
		y.Function = dli.dli_sname;
	else if( y.Function.empty() )
		y.Function = move( symbol );
	if( y.Function.size() )
		y.Function = boost::core::demangle( y.Function.c_str() );
	return y;
#else
	return { frame.source_file(), (uint32_t)frame.source_line(), frame.name() };
#endif
}
#endif
}