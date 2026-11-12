#pragma once

namespace Jde::Opc::OpenProtocol{
	//Above Jde.Opc's tags, bits 32 to 45 (uatypes/Logger.h), with room for that table to grow:  this library links no
	//open62541 and so cannot see it.  Logging::AddTags refuses a bit named twice.
	constexpr uint LogTagBase{ LibraryTagBase+16 };
	constexpr ELogTags LogTag{ (ELogTags)(1ull<<LogTagBase) };
	//What config keys, the ui and the wire call the bit, registered by every main that links the library, with
	//Logging::AddTags before Startup.
	constexpr array<TagName,1> TagNames{{ {"openProtocol", LogTag} }};
	static_assert( NamesBitsFrom(TagNames, LogTagBase), "TagNames names OpenProtocol's one bit" );
}