#include <jde/fwk/exceptions/Exception.h>
#include <jde/fwk/exceptions/CodeException.h>
#include <jde/fwk/exceptions/ExternalException.h>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/log/SpdLog.h>

#define let const auto

namespace Jde::Tests{
	//named apart from the other suites' test exceptions: they all share this namespace, and two different classes
	//under one name across TUs is an ODR violation nothing would report.
	struct TypedException final : Exception{
		TypedException( string what, SRCE )ι:Exception{ move(what), {ELogLevel::Debug}, sl }{}
		α Move()ι->up<Exception> override{ return mu<TypedException>( move(*this) ); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	};

	TEST( ExceptionTests, WhatBadSpecNoTerminate ){
		Exception e{ SRCE_CUR, ELogLevel::Debug, "{:x}", 10 };
		let what = string{ e.what() };
		EXPECT_TRUE( what.contains("10") ) << what;
	}

	TEST( ExceptionTests, WhatGoodSpec ){
		Exception e{ SRCE_CUR, ELogLevel::Debug, "code: {}", 10 };
		EXPECT_EQ( string{e.what()}, "code: 10" );
	}

	//drivers echo user input in their messages - sqlite3_errmsg( 'near "{": syntax error' ), mysql server_message, odbc diagnostics.
	TEST( ExceptionTests, ExternalMessageBraces ){
		ExternalException e{ "near \"{\": syntax error", "select {} from t", {ELogLevel::Debug} };
		EXPECT_EQ( string{e.what()}, "near \"{\": syntax error - select {} from t" );
	}

	//a balanced pair is the worse case: it would consume the description arg, leaving the ' - {}' suffix without one.
	TEST( ExceptionTests, ExternalMessageBalancedBraces ){
		ExternalException e{ "unrecognized token: {}", "insert into t values(?)", {ELogLevel::Debug, ELogTags::Exception, 2067} };
		EXPECT_EQ( string{e.what()}, "(813)unrecognized token: {} - insert into t values(?)" );
	}

	TEST( ExceptionTests, ExternalMessageBracesNoDescription ){
		ExternalException e{ "near \"}\": syntax error", {}, {ELogLevel::Debug} };
		EXPECT_EQ( string{e.what()}, "near \"}\": syntax error" );
	}

	//a copy is silent and keeps the state:  the gateway's LazySlot hands each waiter a copy of the one failure, which logged
	//once, and each waiter's response reads the copy's code and status - UAException::ClientDetail, a UAClientException's
	//mapped HttpStatus.
	TEST( ExceptionTests, ExternalExceptionCopyIsSilentAndKeepsItsState ){
		Logging::ClearMemory();
		{
			ExternalException e{ "near x", "select 1", {ELogLevel::Information, ELogTags::Exception, 2067u, EHttpStatus::Conflict} };
			ExternalException copy{ e };
			EXPECT_EQ( copy.Level(), ELogLevel::NoLog );
			EXPECT_EQ( copy.HttpStatus(), EHttpStatus::Conflict );
			EXPECT_EQ( copy.Code(), 2067u );
			EXPECT_EQ( string{copy.what()}, string{e.what()} );
		}
		//by the template, not the message:  a copy that logged as it was built would log before its args were copied.
		let lines = Logging::Find( [](const Logging::Entry& e){ return e.Text.starts_with("(813)near x"); } );
		EXPECT_EQ( lines.size(), 1u ) << "the original logs once, the copy never";
	}

	TEST( ExceptionTests, HttpStatusDefaults ){
		Exception e{ "boom", {ELogLevel::Debug} };
		EXPECT_EQ( 500u, e.HttpStatus() ); //unset resolves to internal server error.
		EXPECT_EQ( Exception::ECategory::Jde, e.Category() );
		EXPECT_EQ( 0u, e.CategoryCode() );
	}

	//a wire exception loses its concrete type - the stored status set by the reconstructor is all that keeps a 401 a 401, so it has to ride Move().
	TEST( ExceptionTests, HttpStatusSurvivesMove ){
		Exception e{ "denied", {ELogLevel::Debug} };
		e.SetHttpStatus( EHttpStatus::IAmATeapot );
		let moved = e.Move();
		EXPECT_EQ( EHttpStatus::IAmATeapot, moved->HttpStatus() );
	}

	//"preserves dynamic type, unlike a ctor which would slice" - the whole reason FromPtr exists, and its only
	//library caller is MySqlQueryAwait, so nothing in this suite reached it even indirectly.
	TEST( ExceptionTests, FromPtrKeepsType ){
		std::exception_ptr p;
		try{ throw TypedException{"typed"}; }catch( ... ){ p = std::current_exception(); }
		let e = Exception::FromPtr( p );
		ASSERT_NE( e, nullptr );
		EXPECT_NE( dynamic_cast<TypedException*>(e.get()), nullptr ) << "arrived sliced: " << e->what();
		EXPECT_EQ( string{e->what()}, "typed" );
	}

	//the other two branches wrap rather than adopt, and both raise the level to Critical - an exception nobody
	//modelled is a bug in the caller, not an expected outcome.
	TEST( ExceptionTests, FromPtrWrapsForeignExceptions ){
		std::exception_ptr plain;
		try{ throw std::runtime_error{"plain"}; }catch( ... ){ plain = std::current_exception(); }
		let wrapped = Exception::FromPtr( plain );
		ASSERT_NE( wrapped, nullptr );
		EXPECT_EQ( wrapped->Level(), ELogLevel::Critical );
		EXPECT_EQ( string{wrapped->what()}, "std::exception - plain" );

		std::exception_ptr unknown;
		try{ throw 42; }catch( ... ){ unknown = std::current_exception(); }
		let opaque = Exception::FromPtr( unknown );
		ASSERT_NE( opaque, nullptr );
		EXPECT_EQ( opaque->Level(), ELogLevel::Critical );
		EXPECT_EQ( string{opaque->what()}, "unknown exception" );
	}

	//From, for a catch( runtime_error& ):  a Jde exception moves with its type - the gateway's LazySlot copies each waiter's
	//failure by that type, and a UAException's ClientDetail is what the client is shown - and anything else is wrapped.
	TEST( ExceptionTests, FromMovesAJdeTypeAndWrapsTheRest ){
		TypedException typed{ "typed" };
		let kept = Exception::From( move(typed) );
		ASSERT_NE( dynamic_cast<TypedException*>(kept.get()), nullptr ) << "arrived sliced: " << kept->what();
		EXPECT_EQ( string{kept->what()}, "typed" );
		std::runtime_error plain{ "plain" };
		let wrapped = Exception::From( move(plain) );
		EXPECT_EQ( string{wrapped->what()}, "plain" );
		EXPECT_EQ( wrapped->Level(), DefaultExceptionLevel );
	}

	//the adopting ctor: a plain runtime_error contributes only its what(), while an Exception hands over its whole
	//state through operator= - which is what keeps a status code alive across the conversion.
	TEST( ExceptionTests, WrapRuntimeError ){
		Exception plain{ std::runtime_error{"plain"}, {ELogLevel::Debug} };
		EXPECT_EQ( string{plain.what()}, "plain" );

		TypedException inner{ "inner" };
		inner.SetHttpStatus( EHttpStatus::Conflict );
		Exception adopted{ move(inner), {ELogLevel::Debug} };
		EXPECT_EQ( string{adopted.what()}, "inner" );
		EXPECT_EQ( adopted.HttpStatus(), EHttpStatus::Conflict ) << "the Exception branch adopts the source's state, not just its message";
	}

	//the inner-exception ctor with no format:  the inner's text is the message - the body of the gateway's and the hub's
	//401/500 responses, whose RestException passes none.  A format wins, the inner kept but not shown.
	TEST( ExceptionTests, InnerExceptionSuppliesWhatOnlyWithoutAFormat ){
		Exception bare{ SRCE_CUR, {ELogLevel::Debug}, std::logic_error{"inner detail"} };
		EXPECT_EQ( string{bare.what()}, "inner detail" ) << "no format:  the inner's text is the message";
		Exception framed{ SRCE_CUR, {ELogLevel::Debug}, std::logic_error{"inner detail"}, "outer {}", 1 };
		EXPECT_EQ( string{framed.what()}, "outer 1" ) << "a format wins;  the inner is kept but not shown";
	}

	//log once, wherever it happens: at construction when the level warrants it, otherwise at destruction.  Move
	//hands the decision to the target - a double line at every Move()/Throw() would be a visible regression.
	TEST( ExceptionTests, LogsOnce ){
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		constexpr auto message = "logs-once message";
		{
			Exception e{ message, {ELogLevel::Information} };
			let moved = e.Move();
			let movedAgain = moved->Move();
		}//all three destroyed here.
		EXPECT_EQ( logger.Find(message).size(), 1u ) << "the message logged once per Move, not once per object";
	}

	//LogsOnce's other half, the path every shipped config takes:  an exception below BreakLevel, with the exception tag above
	//Trace, is silent at construction and logs from its destructor.  This suite takes the tag at Trace, so it is lowered on
	//the spd logger, which the cumulative follows, and restored however the test ends.
	TEST( ExceptionTests, LogsAtDestructionWhenConstructionWasSilent ){
		auto& spd = Logging::GetLogger<Logging::SpdLog>();
		struct Restore final{ Logging::SpdLog& Spd; ~Restore(){ Spd.ClearLevel( ELogTags::Exception ); } } restore{ spd };
		spd.SetLevel( ELogTags::Exception, ELogLevel::Debug );
		ASSERT_FALSE( Logging::ShouldLog(ELogLevel::Trace, ELogTags::Exception) ) << "another logger still takes the exception tag at Trace";
#ifndef NDEBUG
		ASSERT_GT( Logging::BreakLevel(), ELogLevel::Information ) << "an Information exception would log at construction";
#endif
		auto& memory = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		{
			Exception e{ "deferred to the destructor", {ELogLevel::Information} };
			EXPECT_TRUE( memory.Find("deferred to the destructor").empty() ) << "below BreakLevel and no Trace:  silent at construction";
		}
		EXPECT_EQ( memory.Find("deferred to the destructor").size(), 1u ) << "logged by the destructor";
	}

	//Code() is lazy: unset until asked, then memoized, and derived from the *format* so the same message shares one
	//code whatever its arguments.  IOException::SetWhat guards on HasCode precisely because reading it sets it.
	TEST( ExceptionTests, CodeIsStableHash ){
		Exception a{ SRCE_CUR, ELogLevel::Debug, "stable code {}", 1 };
		Exception b{ SRCE_CUR, ELogLevel::Debug, "stable code {}", 2 };
		EXPECT_FALSE( a.HasCode() );
		EXPECT_EQ( a.Code(), b.Code() ) << "hashed over the format, not the arguments";
		EXPECT_TRUE( a.HasCode() ) << "Code() memoizes into _code";

		Exception other{ SRCE_CUR, ELogLevel::Debug, "a different format" };
		EXPECT_NE( other.Code(), a.Code() );

		Exception explicitCode{ "boom", {ELogLevel::Debug, ELogTags::Exception, 42u} };
		EXPECT_TRUE( explicitCode.HasCode() );
		EXPECT_EQ( explicitCode.Code(), 42u ) << "an explicit code must not be replaced by the hash";
	}

	TEST( ExceptionTests, PrependWhat ){
		Exception e{ SRCE_CUR, ELogLevel::Debug, "message {}", 1 };
		e.PrependWhat( "prefix: " );
		EXPECT_EQ( string{e.what()}, "prefix: message 1" ) << "the format is rendered first, then prefixed";
		EXPECT_TRUE( e.ClientDetail().empty() ) << "internals stay in what() unless a subclass opts in";
	}

	TEST( ExceptionTests, IOExceptionWhat ){
		IO::IOException coded{ fs::path{"/x/y.txt"}, 2u, "Could not open file" };
		let what = string{ coded.what() };
		EXPECT_TRUE( what.contains("y.txt") ) << what;
		EXPECT_TRUE( what.contains("Could not open file") ) << what;
		EXPECT_TRUE( what.contains(std::system_category().message(2)) ) << "the code's system message is spelled out for the reader: " << what;
		EXPECT_EQ( coded.Path(), fs::path{"/x/y.txt"} );

		IO::IOException uncoded{ fs::path{"/x/y.txt"}, "no code here" };
		EXPECT_EQ( string{uncoded.what()}, "no code here path='/x/y.txt'" ) << "no code - no system message, and Code() must not have been read";
	}

	TEST( ExceptionTests, CodeExceptionToString ){
		let code = std::error_code{ 2, std::system_category() };
		EXPECT_EQ( CodeException::ToString(code), Ƒ("{} - {}", code.category().name(), code.message()) );
		CodeException e{ code, ELogTags::Test, ELogLevel::Debug };
		EXPECT_EQ( e.Code(), 2u ) << "the error_code's value rides as the exception code";
		EXPECT_TRUE( string{e.what()}.contains(code.message()) ) << e.what();
	}
}