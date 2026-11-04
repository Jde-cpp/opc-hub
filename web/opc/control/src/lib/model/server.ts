import { PropertyNames } from 'jde-framework';
import { ServerCnnctn, ServerCnnctnProps } from './server-cnnctn.js';

export class ServerDesc{
	constructor( obj:ServerDescProps ){
		this.applicationUri = obj.applicationUri;
		this.productUri = obj.productUri;
		this.applicationName = obj.applicationName;
		this.applicationType = obj.applicationType;
		this.gatewayServerUri = obj.gatewayServerUri;
		this.discoveryProfileUri = obj.discoveryProfileUri;
		this.discoveryUrls = obj.discoveryUrls ?? [];
	}
	applicationUri: string;
	productUri: string;
	applicationName: string;
	applicationType: string;
	gatewayServerUri: string;
	discoveryProfileUri: string;
	discoveryUrls: string[];
	get accessResource():string{ return `${this.applicationName.substring(this.applicationName.indexOf('[')+1,this.applicationName.indexOf(']'))}`; }
}
export type ServerDescProps = Pick<ServerDesc, PropertyNames<ServerDesc>>;
//what the resolver's describe (OpcStore.getConnection) and Test Connection both select, so a ServerDesc field reaches both
export const serverDescColumns = "applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls";
export const namespaceColumns = "index uri";

/** An entry of the server's namespace array;  `index` is the `ns` of every NodeId in it. */
export type Namespace = {
	index: number;
	uri: string;
};

export class Server extends ServerDesc{
	constructor( obj:ServerProps ){
		super( obj.desc );
		this.connection = new ServerCnnctn( obj.connection );
		this.policy = obj.policy;
		this.mode = obj.mode;
		this.namespaces = obj.namespaces ?? [];
	}
	uri( ns:number ):string|undefined{ return this.namespaces.find( (x)=>x.index==ns )?.uri; }
	connection: ServerCnnctn;
	policy: string;
	mode: string;
	namespaces: Namespace[];
}
export type ServerProps = {
	connection: ServerCnnctnProps;
	desc: ServerDescProps;
	policy: string;
	mode: string;
	namespaces: Namespace[];
};
/** What the gateway's `testConnection` answers for settings that are not saved:  the description, the policy and mode the
 * session was opened with, and the namespaces - a Server without its connection row (client-detail's Test Connection). */
export type TestConnectionResult = ServerDescProps & { securityPolicyUri: string; securityMode: string; namespaces: Namespace[] };

//The certificate panel's two fields on serverConnection (docs/clients/spec.md *Trust*, #415), read from the row - no connection,
//so they survive a connect that fails.  rejectedCertificate is the server certificate the gateway's verifier last rejected for
//the slug, null when there is none;  trustServer trusts it by its thumbprint.  clientCertificate is the gateway's own issued
//certificate for the connection, the one it presents, null when its file does not exist;  `pem` is the file for the page to
//offer as a download, so the help's copy-by-path (assets/help/gateways.md) becomes a button.
export type RejectedCertificate = { subject:string; issuer:string; notBefore:string; notAfter:string; uri:string; thumbprint:string };
export type ClientCertificate = { path:string; subject:string; notBefore:string; notAfter:string; uri:string; pem:string };
export type ConnectionCertificates = { rejectedCertificate:RejectedCertificate|null; clientCertificate:ClientCertificate|null };
//read from the row (ServerCnnctnSessionsQLAwait, no ConnectAwait), so it answers whether or not the gateway can connect.
export const connectionCertificatesQuery = "serverConnection( slug:$opc, deleted:null ){ rejectedCertificate{ subject issuer notBefore notAfter uri thumbprint } clientCertificate{ path subject notBefore notAfter uri pem } }";
//a system mutation under Administer, POSTed:  it changes the gateway's trust list.  It takes no selection - trustServer answers a scalar.
export const trustServerMutation = "mutation trustServer( opc:$opc, thumbprint:$thumbprint )";