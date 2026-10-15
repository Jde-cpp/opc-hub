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
	get opcSlug():string{ return this.connection.slug; }
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