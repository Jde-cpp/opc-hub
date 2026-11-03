import { serverDescColumns } from './server';
import { securedPolicies, securityModes, securityPolicyNone } from './server-cnnctn';

//What the gateway's `discover` answers (docs/clients/spec.md *Discovery*):  `discover( url, servers: true )` the servers FindServers
//lists at a discovery url - a Local Discovery Server's, or any server's, which answers with itself - each an ApplicationDescription
//as serverDescription answers one;  `discover( url )` the endpoints GetEndpoints lists at a server url, each with the server's
//applicationUri, its policy and mode, the server's own ranking, the token policies it takes and the certificate it presents,
//decoded - null on an endpoint that carries none, `error` on one the gateway could not decode.
export type DiscoveredServer = { applicationUri:string; productUri:string; applicationName:string; applicationType:string; gatewayServerUri:string; discoveryProfileUri:string; discoveryUrls:string[] };//ServerDesc's fields, as a plain row
export type DiscoveredTokenPolicy = { type:string; policy:string; policyId:string };
export type DiscoveredCertificate = { subject:string; issuer:string; notBefore:string; notAfter:string; uri:string; thumbprint:string; error?:string };
export type DiscoveredEndpoint = { url:string; applicationUri:string; policy:string; mode:string; securityLevel:number; tokenPolicies:DiscoveredTokenPolicy[]; certificate:DiscoveredCertificate|null };
//mutations, POSTed:  they connect the gateway to a url of the caller's choosing, which a GET must not do.
export const discoverServersMutation = `mutation discover( url: $url, servers: true ){ ${serverDescColumns} }`;
export const discoverEndpointsMutation = "mutation discover( url: $url ){ url applicationUri policy mode securityLevel tokenPolicies{ type policy policyId } certificate{ subject issuer notBefore notAfter uri thumbprint error } }";
//What a pick fills the connection form with:  the endpoint's url - with the host of the url that answered, `at`, where the endpoint
//names another (endpointUrl) - the Certificate URI with the server's applicationUri, and the mode and policy of the spec's
//connection settings, pinned to the endpoint's.
export type EndpointPick = { url:string; certificateUri:string; securityMode:string; securityPolicy:string };
export const endpointPick = ( ep:DiscoveredEndpoint, at?:string ):EndpointPick=>({ url: endpointUrl( ep.url, at ), certificateUri: ep.applicationUri, securityMode: ep.mode, securityPolicy: ep.policy });
//An opc.tcp url's host - a name, an address, or an IPv6 one in brackets - and what comes before and after it.
const urlParts = ( url:string )=>{ const m = /^(opc\.tcp:\/\/)(\[[^\]]*\]|[^:/]*)(.*)$/i.exec( url.trim() ); return m?.[2] ? { scheme: m[1], host: m[2], rest: m[3] } : undefined; };
//The url a pick connects to:  a server advertises the urls it was configured with, which the gateway may not reach - a host name
//behind NAT - so where the endpoint's host is not the one GetEndpoints answered at, that host goes in its place, the endpoint's port
//and path kept, as UA clients do.
export const endpointUrl = ( url:string, at?:string ):string=>{
	const ep = urlParts( url ), answered = at ? urlParts( at ) : undefined;
	return ep && answered && ep.host.toLowerCase()!=answered.host.toLowerCase() ? `${ep.scheme}${answered.host}${ep.rest}` : url;
};
//An endpoint the gateway can connect to:  a mode it sets and a policy it carries (ClientSecurity::SecuredPolicyUris), or None -
//anything else the save refuses.
export const supportedEndpoint = ( ep:DiscoveredEndpoint ):boolean=>(securityModes as readonly string[]).includes( ep.mode ) && (ep.policy==securityPolicyNone || securedPolicies.includes( ep.policy ));
//The url GetEndpoints goes to for a server FindServers listed:  its first opc.tcp discovery url, else the one typed.  A server
//advertises the urls it was configured with, which the gateway may not reach - a host name behind NAT - so the dialog shows which it
//is going to and lets the typed one be tried in its place.
export const serverUrl = ( server:DiscoveredServer, typed:string ):string=>server.discoveryUrls?.find( u=>u.startsWith( 'opc.tcp://' ) ) ?? typed;