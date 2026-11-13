#include "DriverServer.h"
#include "../GatewayAppClient.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };

	α DriverServer::Settings( sv kind, const ServerCnnctnNK& slug, PortType port, SL sl )ε->Opc::Server::UASettings{
		let& ssl = AppClient()->SslSettings;
		THROW_IFSL( !ssl || ssl->Certificate.SanUri().empty(), "Connection '{}' has no server of the gateway's own:  the process's certificate, /http/ssl, needs a URI in its subjectAltName, the gateway's applicationUri.", slug );
		//The gateway issues the slug's certificate on a web user's first connect, and again when it expires:  made now, so the list
		//has it from the start, and read again by its path when a session presents one it has not seen.  A slug with none to make -
		//an operator's own pair, its file not there yet - keeps the collector's sessions, which present the process's.
		try{
			UAClient::EnsureCertificate( slug, sl );
		}
		catch( const Exception& e ){
			WARN( "Connection '{}':  its server admits no web session until the slug's certificate is there:  {}", slug, e.what() );
		}
		Opc::Server::UASettings y{ .Ssl{ *ssl } };
		y.ApplicationName = Ƒ( "Jde-Cpp {} {}", kind, slug );
		y.Port = port;
		y.Address = "127.0.0.1";
		y.TrustedCerts = { UAClient::CryptoSettings(slug).Certificate.Path, ssl->Certificate.Path };
		y.None = false;
		y.EncryptOnly = true;
		y.OwnCertificate = CertificateUser();
		return y;
	}
	α DriverServer::Anchors()ι->vector<fs::path>{
		let& ssl = AppClient()->SslSettings;
		return ssl ? vector<fs::path>{ ssl->Certificate.Path } : vector<fs::path>{};
	}
}