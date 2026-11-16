import { ELogLevel } from 'jde-proto/Log';

export interface Instance{
	application?:string;
	host:string;
	pid?:number;
	dbDefaultLogLevel?:ELogLevel;
	fileDefaultLogLevel?:ELogLevel;
	startTime?:Date;
	port?:number;
	instanceName?:string;
}
//The environment's applicationServer.  Its port is null where the site's config.json says so (docs/demo/spec.md *The public
//origin*), a front answering for the hub on the page's own port;  listenerPort is then the hub's own, the build's port.
export interface AppServerEnvironment extends Omit<Instance,'port'>{
	port?:number|null;
	listenerPort?:number;
}

//The host a browser reaches an instance by (reviews/install-issues.md #2).  An empty host - the production environment's
//applicationServer - is the page's own host.  A loopback name - what the hub advertises for itself over /opcGateways and
///opcServers (`http.host: "localhost"`), and what the development environment configures - reaches the instance only from
//its own machine, and the page's host reaches that same machine, so it is rewritten to the page's host:  a page at
//http://hub:8071 then calls hub:1967 rather than the browser's own localhost, and the server's allowOrigin 'sameHost'
//holds, Origin and Host naming the same host.  Any other name is one the registry vouches for - a split deployment - and
//is kept;  ProtoService's instances setter warns when it differs from the page's.
export const loopbackHosts:ReadonlySet<string> = new Set( ["localhost", "127.0.0.1", "::1", "[::1]"] );
export function resolveInstanceHost( host:string|undefined, pageHost:string ):string{
	if( !host )
		return pageHost || "localhost";
	return pageHost && loopbackHosts.has( host.toLowerCase() ) ? pageHost : host;
}
//The port the page was browsed by:  location.port is empty on the scheme's own.
export function pagePort( page:Pick<Location,'port'|'protocol'>|undefined = typeof location=="undefined" ? undefined : location ):number|undefined{
	return page ? +page.port || ( page.protocol=="https:" ? 443 : 80 ) : undefined;
}
//A front answering on the page's own port for the hub's listener, where config.json's applicationServer.port is null:  AppService
//sets it from the environment before any instance is resolved, and every instance resolved after takes it, ProtoService's setter
//included.
export type Front = { listener:number; page:number };
let pageFront:Front|undefined;
export function setFront( front:Front|undefined ):void{ pageFront = front; }
//Behind a front, an instance of the hub's own process - on an empty or loopback host at the hub's listener port, the gateway
///opcGateways names at localhost:1967 among them - takes the page's port as it takes the page's host.  Another process on the
//box, at another port, keeps its own, as does an instance on another host, a split gateway.
export function resolveInstance<T extends Instance>( instance:T, pageHost:string = typeof location=="undefined" ? "" : location.hostname, front:Front|undefined = pageFront ):T{
	const host = resolveInstanceHost( instance.host, pageHost );
	const own = !instance.host || loopbackHosts.has( instance.host.toLowerCase() );
	const port = front && own && instance.port==front.listener ? front.page : instance.port;
	return host==instance.host && port==instance.port ? instance : { ...instance, host, port };
}