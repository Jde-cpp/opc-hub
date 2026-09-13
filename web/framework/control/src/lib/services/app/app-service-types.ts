import * as FromServer from 'jde-proto/App.FromServer';
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
//is kept; the Gateway constructor warns when it differs from the page's.
export const loopbackHosts:ReadonlySet<string> = new Set( ["localhost", "127.0.0.1", "::1", "[::1]"] );
export function resolveInstanceHost( host:string|undefined, pageHost:string ):string{
	if( !host )
		return pageHost || "localhost";
	return pageHost && loopbackHosts.has( host.toLowerCase() ) ? pageHost : host;
}
export function resolveInstance<T extends Instance>( instance:T, pageHost:string = typeof location=="undefined" ? "" : location.hostname ):T{
	const host = resolveInstanceHost( instance.host, pageHost );
	return host==instance.host ? instance : { ...instance, host };
}
