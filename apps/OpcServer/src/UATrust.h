#pragma once
#include <open62541/server.h>
#include "jde/fwk/usings.h"
#include <jde/opc/uatypes/Logger.h>

//Runtime reload of /access/trustedCertDirs for the UA PKI groups, so a client cert re-issued after startup (the
//gateway rewrites its file in place on a certificateUri change) heals without a server restart - mirrors
//Access::Server::TrustVerify/loadTrustAnchors (libs/access serverInternal.cpp).
namespace Jde::Opc::Server::UATrust{
	α LoadTrustList( UA_TrustListDataType& list )ι->bool;//fills TRUSTEDCERTIFICATES from the dirs (mtime-cached); true if any file was added/changed/removed since the last call. Caller clears list.
	α Install( UA_Server& ua )ι->void;//wraps secureChannelPKI+sessionPKI verifyCertificate; call once after UA_Server_newWithConfig, before Run().
}
