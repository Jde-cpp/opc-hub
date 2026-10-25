//The gateway's own OPC UA endpoint as endpoint{…} answers it (apps/OpcGateway/src/ql/OpcSessionsQLAwait.h, servers spec *Web*):
//null where the gateway has no /gateway/server block.  Two queries, as the sessions are gated (gateway/sessions) and the rest is
//not:  a refusal of the sessions leaves the url and policies to show.
export type EndpointPolicy = { uri:string; mode:string };
export type EndpointSession = { id:string; type:string; policy:string; mode:string; since:string; user:{ id:number; name?:string }|null };
export type GatewayEndpoint = { url:string; policies:EndpointPolicy[]; trustedCertDirs:string[]; rejectedCertDir:string|null };
export const endpointQuery = "endpoint{ url policies{ uri mode } trustedCertDirs rejectedCertDir }";
export const endpointSessionsQuery = "endpoint{ sessions{ id type policy mode since user{ id name } } }";
//A policy by its short name, what follows the '#' of its URI:  Basic256Sha256, None.
export const policyName = ( uri:string ):string=>uri.substring( uri.lastIndexOf( '#' )+1 );
//Where the endpoint serves a connection's groups' members (servers spec *Address space*):  under the connection's folder, each at its
//browse path on the source - the path's names when given, else the folder alone.
export const servedPath = ( slug:string, names?:string[] ):string=>['Objects', slug, ...(names ?? ['…'])].join( '/' );