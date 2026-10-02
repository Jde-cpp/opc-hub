#pragma once
#include <jde/fwk/str.h>
#include <absl/strings/str_replace.h>
#include "Exception.h"

#define let const auto

namespace Jde{
	struct ExternalException : Exception{
		ExternalException( const ExternalException& from )ι:
			Exception{ string{from.Format()}, {ELogLevel::NoLog, from.Tags, from._code, from._statusCode}, from._sl }{
			_args = from._args;
		}
		ExternalException( ExternalException&& from )ι=default;

		//which base the library documents its codes in: UA status and OpenSSL errors are quoted in hex, engine codes in decimal (sqlite 2067, mysql 1062, mssql 2627).
		enum class ECodeBase : uint8{ Hex, Decimal };

		ExternalException( string externalMessage, string description, ExceptionArgs args, SRCE )ι:
			ExternalException{ ECodeBase::Hex, move(externalMessage), move(description), args, sl }{}

		ExternalException( ECodeBase codeBase, string externalMessage, string description, ExceptionArgs args, SRCE )ι:
			Exception{ FormatMsg(codeBase, move(externalMessage), description.size(), args), {ELogLevel::NoLog, args.Tags, args._code, args._statusCode}, sl }{
			if( description.size() ){
				_what = Str::TryFormat( Format(), {description} );//never throw out of a ι ctor: Str::Format is ε.
				_args.push_back( move(description) );
			}
			SetLevel( args.Level() );
			BreakLog();
		}
	private:
		//error code is part of format; ' - {}' description suffix only when a description exists.
		Ω FormatMsg( ECodeBase codeBase, string&& externalMessage, bool hasDescription, const ExceptionArgs& args )ι->string{
			const sv suffix = hasDescription ? " - {}" : "";
			//with a description the result is itself a format string, and external libraries echo user input (sql tokens, uris, node ids) - braces in it must not be read as fields. Without one there are no args, so Exception::what/Entry::Message use it verbatim and escaping would leak '{{'.
			let message = hasDescription ? absl::StrReplaceAll( externalMessage, {{"{", "{{"}, {"}", "}}"}} ) : move(externalMessage);
			if( !args.HasCode() )
				return Ƒ( "{}{}", message, suffix );
			return Ƒ("({}){}{}", codeBase==ECodeBase::Hex ? hex(args._code) : std::to_string(args._code), message, suffix);
		}
	};
}
#undef let