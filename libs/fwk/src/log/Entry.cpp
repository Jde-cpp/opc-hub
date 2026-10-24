#include <jde/fwk/log/Entry.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/fwk/crypto/digest.h>
#include <spdlog/common.h>	//Entry.h only forward-declares source_loc; SourceLocation() is defined here.

namespace Jde::Logging{
	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, string&& m, vector<string> args )ι:
		Entry( sl, l, tags, {}, move(m), move(args) )
	{}

	Entry::Entry( SL sl, ELogLevel l, ELogTags tags, Jde::UserPK userPK, string&& m, vector<string> args )ι:
		Text{ move(m) },
		Arguments{ move(args) },
		Level{ l },
		Tags{ tags },
		Line{ sl.line() },
		Time{ Clock::now() },
		UserPK{ userPK },
		_file{ sv{sl.file_name()} },//static storage - copying into strings costs 2 allocations per log call.
		_function{ sv{sl.function_name()} }
	{}
	Entry::Entry( ELogLevel l, ELogTags tags, uint32_t line, TimePoint time, Jde::UserPK userId, uuid messageId, uuid fileId, uuid functionId, vector<string>&& args )ι:
		Arguments{ move(args) },
		Level{ l },
		Tags{ tags },
		Line{ line },
		Time{ time },
		UserPK{ userId },
		_file{ fileId },
		_function{ functionId },
		_id{ messageId }
	{}
	Entry::Entry( ELogLevel l, ELogTags tags, uint32_t line, TimePoint time, Jde::UserPK userId, string&& text, string&& file, string&& function, vector<string>&& args )ι:
		Text{ move(text) },
		Arguments{ move(args) },
		Level{ l },
		Tags{ tags },
		Line{ line },
		Time{ time },
		UserPK{ userId },
		_file{ move(file) },
		_function{ move(function) }
	{}

	α GenerateId( sv text )ι->StringMd5{ return Crypto::CalcMd5(text); }

	α Entry::SourceLocation()Ι->spdlog::source_loc{
		return spdlog::source_loc{
			_file.View().data(),//NUL-terminated either way: a source_location literal, or the owned string's buffer.
			(int)Line,
			_function.View().data()
		};
	}
	α Entry::Message()Ι->string{
		if( _message.size() )
			return _message;
    fmt::dynamic_format_arg_store<fmt::format_context> store;
		for( auto&& arg : Arguments ){
			store.push_back( arg );
		}
		try{
			_message = Arguments.size()==0 ? Text : fmt::vformat(Text, store);
		}
		catch( const runtime_error& e ){
			CRITICALT( Tags, "Bad Format: {}, args: '{}', what: '{}'", Text, Str::Join(Arguments, ", ", true), e.what() );
			_message = Ƒ( "{}, args: '{}'", Text, Str::Join(Arguments, ", ", true) );
		}
    return _message;
	}
}