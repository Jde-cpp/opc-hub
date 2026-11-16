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
//`pagePort` is given where the site's config.json says applicationServer.port null (docs/demo/spec.md *The public origin*):  a
//front answers for the hub on the page's own port, so every instance on an empty or loopback host - the hub's own process,
//the gateway /opcGateways names at localhost:1967 too - takes the page's port as it takes the page's host.  An instance on
//another host, a split gateway, keeps its own.
export function resolveInstance<T extends Instance>( instance:T, pageHost:string = typeof location=="undefined" ? "" : location.hostname, pagePort?:number ):T{
	const host = resolveInstanceHost( instance.host, pageHost );
	const own = !instance.host || loopbackHosts.has( instance.host.toLowerCase() );
	const port = own && pagePort ? pagePort : instance.port;
	return host==instance.host && port==instance.port ? instance : { ...instance, host, port };
}