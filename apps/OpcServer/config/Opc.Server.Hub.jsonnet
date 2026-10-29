// Opc.Server.jsonnet against a Jde.Opc.Hub instead of a split AppServer: the login TLS anchor is the hub's http cert
// (Opc.Hub.jsonnet's /http/app commonName "OpcHub").  Everything else - port 1970, the registry it logs in to on 1967 - is
// unchanged.  caFile is a single path (libs/web/client/ClientSsl.cpp), which is why this is an overlay, not a second entry.
local args = import 'args.libsonnet';
local base = import 'Opc.Server.jsonnet';
function( sync=false ) base( sync ) + {
	web+:{ client+:{ ssl+:{ caFile: args.certsDir("OpcHub")+"/OpcHub.pem" } } },
	opcServer+:{ hist:: super.hist } //off:  against a hub the gateway's groups keep the history.  Un-hide it to have the server keep its own again.
}