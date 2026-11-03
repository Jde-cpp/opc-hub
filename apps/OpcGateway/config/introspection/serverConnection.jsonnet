//Live fields the gateway grafts onto the DB-backed ServerConnection rows (ql/OpcSessionsQLAwait.cpp: ServerCnnctnSessionsQLAwait answers
//`serverConnections{ … opcSessions{count} }`). `extend: true` appends these to the introspected server_connections columns instead of replacing them.
local types = import 'scalars.libsonnet';
local String = types.String, UInt = types.UInt, NonNull = types.NonNull;
local serverConnection = {
	extend: true,
	fields: [
		{ name: "connectionStatus", type: { kind: "OBJECT", name: "ConnectionStatus" } },
		{ name: "opcSessions", type: { kind: "OBJECT", name: "OpcSessions" } },
		{ name: "opcConnections", type: { kind: "OBJECT", name: "OpcConnections" } },
		{ name: "rejectedCertificate", type: { kind: "OBJECT", name: "RejectedCertificate" } },
		{ name: "clientCertificate", type: { kind: "OBJECT", name: "ClientCertificate" } }
	]
};
{
	ServerConnection: serverConnection, //the web client's canonical name (schemaWithEnums("ServerConnection")).
	serverConnections: serverConnection, //the query's name.
	OpcSessions: { //config-only types: no view, so __type(name:...) is answered from here alone.
		fields: [
			{ name: "count", type: NonNull(UInt) }
		]
	},
	OpcConnections: {
		fields: [
			{ name: "count", type: NonNull(UInt) }
		]
	},
	ConnectionStatus: { //Connected|Idle|Error, from the live UAClients and UAClient::ConnectErrors() - `error` carries why the last attempt failed.
		fields: [
			{ name: "name", type: NonNull(String) },
			{ name: "error", type: String }
		]
	},
	RejectedCertificate: { //the server certificate our verifier rejected for this slug (ServerTrust), null when there is none;  trustServer moves its file into a trusted dir
		fields: [
			{ name: "subject", type: NonNull(String) },
			{ name: "issuer", type: NonNull(String) },
			{ name: "notBefore", type: NonNull(String) },
			{ name: "notAfter", type: NonNull(String) },
			{ name: "uri", type: NonNull(String) },
			{ name: "thumbprint", type: NonNull(String) } //the SHA-1, the name of the .der and the argument trustServer takes
		]
	},
	ClientCertificate: { //the gateway's own issued certificate for this connection, the one it presents, null when its file does not exist;  `pem` is the file for the page to download
		fields: [
			{ name: "path", type: NonNull(String) },
			{ name: "subject", type: NonNull(String) },
			{ name: "notBefore", type: NonNull(String) },
			{ name: "notAfter", type: NonNull(String) },
			{ name: "uri", type: NonNull(String) },
			{ name: "pem", type: NonNull(String) }
		]
	}
}