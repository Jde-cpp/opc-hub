#pragma once
#ifndef LOG_TAGS_H
#define LOG_TAGS_H
#include <span>

namespace Jde{
	#define Φ Γ auto
	enum class ELogTags : uint{
		None 					= 0x0,
		Access				= 1ul << 0,
		App 					= 1ul << 1,
		Cache	 	  		= 1ul << 2,
		Client  			= 1ul << 3,
		Crypto				= 1ul << 4,
		DBDriver			= 1ul << 5,
		Exception			= 1ul << 6,
		ExternalLogger= 1ul << 7,
		Http  				= 1ul << 8,
		IO			 			= 1ul << 9,
		Locks	 	  		= 1ul << 10,
		Parsing 			= 1ul << 11,
		Pedantic 			= 1ul << 12,
		QL			 			= 1ul << 13,
		Read	  			= 1ul << 14,
		Scheduler	 		= 1ul << 15,
		Server  			= 1ul << 16,
		Sessions  		= 1ul << 17,
		Settings 			= 1ul << 18,
		Shutdown 			= 1ul << 19,
		Socket  			= 1ul << 20,
		Sql						= 1ul << 21,
		Startup 			= 1ul << 22,
		Subscription 	= 1ul << 23,
		Test					= 1ul << 24,
		Threads				= 1ul << 25,
		Write		 			= 1ul << 26,
		Flow					= 1ul << 27,
		All						= ~0ull,

		HttpClientRead	  = Http | Client | Read,
		HttpClientWrite		= Http | Client | Write,
		HttpClientSessions= Http | Client | Sessions,
		HttpServerRead	  = Http | Server | Read,
		HttpServerWrite		= Http | Server | Write,
		SocketClientRead  = Socket | Client | Read,
		SocketClientReadSub  = Socket | Client | Read | Subscription,
		SocketClientWrite	= Socket | Client | Write,
		SocketClientWriteSub	= Socket | Client | Write | Subscription,
		SocketServerRead  = Socket | Server | Read,
		SocketServerWrite	= Socket | Server | Write
	};
	//A tag's name in config keys, the ui and the wire, bound to its bit rather than to a place in a list.
	struct TagName{ sv Name; ELogTags Tag; };
	//Whether names[i] is bit firstBit+i for every i - the static_assert each table makes, so a gap, a repeat, a composite
	//or an entry out of bit order fails to compile.  ToValue spells a tag set in table order.
	constexpr α NamesBitsFrom( std::span<const TagName> names, uint firstBit )ι->bool{
		for( uint i=0; i<names.size(); ++i ){
			if( underlying(names[i].Tag)!=1ull<<(firstBit+i) )
				return false;
		}
		return true;
	}
	//A new tag takes the bit after Flow, and its name goes last here.
	constexpr array<TagName,28> FwkTags{{
		{"access", ELogTags::Access}, {"app", ELogTags::App}, {"cache", ELogTags::Cache}, {"client", ELogTags::Client},
		{"crypto", ELogTags::Crypto}, {"dbDriver", ELogTags::DBDriver}, {"exception", ELogTags::Exception},
		{"externalLogger", ELogTags::ExternalLogger}, {"http", ELogTags::Http}, {"io", ELogTags::IO}, {"locks", ELogTags::Locks},
		{"parsing", ELogTags::Parsing}, {"pedantic", ELogTags::Pedantic}, {"ql", ELogTags::QL}, {"read", ELogTags::Read},
		{"scheduler", ELogTags::Scheduler}, {"server", ELogTags::Server}, {"sessions", ELogTags::Sessions},
		{"settings", ELogTags::Settings}, {"shutdown", ELogTags::Shutdown}, {"socket", ELogTags::Socket}, {"sql", ELogTags::Sql},
		{"startup", ELogTags::Startup}, {"subscription", ELogTags::Subscription}, {"test", ELogTags::Test},
		{"threads", ELogTags::Threads}, {"write", ELogTags::Write}, {"flow", ELogTags::Flow}
	}};
	static_assert( NamesBitsFrom(FwkTags, 0), "FwkTags names Access to Flow, one bit each, in bit order" );
	//The combined tags the ui offers as rows of their own, beside the single ones:  Logging::Tags( true ).
	constexpr array<ELogTags,11> CompositeTags{
		ELogTags::HttpClientRead, ELogTags::HttpClientWrite, ELogTags::HttpClientSessions,
		ELogTags::HttpServerRead, ELogTags::HttpServerWrite,
		ELogTags::SocketClientRead, ELogTags::SocketClientReadSub,
		ELogTags::SocketClientWrite, ELogTags::SocketClientWriteSub,
		ELogTags::SocketServerRead, ELogTags::SocketServerWrite
	};
	//The framework's tags take the low 32 bits;  from here up are the libraries' own, EOpcLogTags first.
	constexpr uint LibraryTagBase{ 32 };
	//A line at the break level traps the debugger unless it carries a library's tag:  open62541's and the OPC code's
	//warnings are routine.  Decided by bit, on every path, not by the tags' magnitude.
	constexpr α BreaksFor( ELogTags tags )ι->bool{ return ( underlying(tags) >> LibraryTagBase )==0; }

	constexpr ELogTags DefaultTag=ELogTags::App;
	constexpr sv TagSeparator{ "." };//how a combined tag is spelled as one name: "socket.client.read" - config keys, ui rows and the joined form of the wire's tag array.
	Φ ToName( ELogTags tags )ι->string;//one name, the parts joined with TagSeparator in bit order:  config keys, ui rows and log text.
	Φ ToValue( ELogTags tags )ι->jvalue;
	Φ ToLogTags( sv name )ι->ELogTags;
	Φ ToLogTags( jvalue v )ι->ELogTags;
namespace Logging{
	//A library's names for its tags, a table whose own static_assert puts it above LibraryTagBase:  before Init, which parses
	//the settings' tag names, as nothing locks the table once lines are being logged.  A bit or name already registered is a
	//logic_error, thrown before anything is added - before Init there is no logger to say so.
	Φ AddTags( std::span<const TagName> tags )ε->void;
	Φ Tags( bool user=false )ι->flat_map<string,uint>;
}}
#undef Φ
#endif