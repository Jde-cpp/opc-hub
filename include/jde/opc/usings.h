#pragma once
#ifndef JDE_OPC_USINGS
#define JDE_OPC_USINGS
#include <jde/db/usings.h>

namespace Jde::Opc{
	using boost::uuids::uuid;

	using NsIndex = UA_UInt16;
	using StatusCode = UA_StatusCode;
	using VariantPK = uint32;
	using BrowseNamePK = uint32;
}
#endif