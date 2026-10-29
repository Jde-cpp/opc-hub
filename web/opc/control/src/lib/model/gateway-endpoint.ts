//The gateway's own OPC UA endpoint as endpoint{…} answers it (apps/OpcGateway/src/ql/OpcSessionsQLAwait.h, servers spec *Web*):
//null where the gateway has no /gateway/server block.  Its own gate, gateway/endpoint;  the sessions gateway/sessions as well, so
//they're read apart:  a refusal of the sessions leaves the url and policies to show.
export type EndpointPolicy = { uri:string; mode:string };
export type EndpointSession = { id:string; type:string; policy:string; mode:string; since:string; user:{ id:number; name?:string }|null };
export type EndpointDescription = { url:string; policies:EndpointPolicy[]; trustedCertDirs:string[]; rejectedCertDir:string|null };
//A group's member as the endpoint serves it:  by its hist_group_nodes id, its variable's NodeId there and its browse path from Objects.
export type EndpointServedNode = { id:number; nodeId:string; path:string };
//What a group's page reads:  the url, and the group's members as served, null for a group the endpoint doesn't serve - left out
//for a new group, which asks the url alone.
export type EndpointServed = { url:string; served?:EndpointServedNode[]|null };
export const endpointQuery = "endpoint{ url policies{ uri mode } trustedCertDirs rejectedCertDir }";
export const endpointSessionsQuery = "endpoint{ sessions{ id type policy mode since user{ id name } } }";
export const endpointUrlQuery = "endpoint{ url }";
export const endpointServedQuery = "endpoint{ url served( group: $groupId ){ id nodeId path } }";
/** `http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256` → `Basic256Sha256`:  every policy shares the prefix, so the
 * fragment is the part that says anything;  a uri without one (or an empty one) is shown as it came. */
export const policyName = ( uri:string ):string=>uri.substring( uri.lastIndexOf( '#' )+1 );
/** The wire name of the message security mode as words;  anything not listed is shown as it came. */
export const modeName = ( mode:string ):string=>({ SignAndEncrypt: "Sign & Encrypt" } as Record<string,string>)[mode] ?? mode;
//The folder the endpoint serves a connection's groups' members under (servers spec *Address space*).
export const servedFolder = ( slug:string ):string=>`Objects/${slug}`;
//The deepest folder a group's served members share, from the endpoint's own paths:  undefined for none served.
export function servedUnder( served:EndpointServedNode[] ):string|undefined{
	let shared:string[]|undefined;
	for( const folder of served.map( n=>n.path.split( '/' ).slice( 0, -1 ) ) ){
		let i = 0;
		while( shared && i<shared.length && shared[i]==folder[i] )
			++i;
		shared = shared ? shared.slice( 0, i ) : folder;
	}
	return shared?.join( '/' );
}