//Live fields the gateway grafts onto the DB-backed ServerConnection rows (ql/OpcSessionsQLAwait.cpp: ServerCnnctnSessionsQLAwait answers
//`serverConnections{ … opcSessions{count} }`). `extend: true` appends these to the introspected server_connections columns instead of replacing them.
local types = import 'scalars.libsonnet';
local String = types.String, UInt = types.UInt, NonNull = types.NonNull;
local serverConnection = {
	extend: true,
	fields: [
		{ name: "connectionStatus", type: { kind: "OBJECT", name: "ConnectionStatus" } },
		{ name: "opcSessions", type: { kind: "OBJECT", name: "OpcSessions" } },
		{ name: "opcConnections", type: { kind: "OBJECT", name: "OpcConnections" } }
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
	}
}