import { Route, Routes } from '@angular/router';

//The site's catch-all page for a url no route matches (data.notFound, reviews/m3-closing.md #38).  It matches everything, so
//whatever reads the route table for what exists - the crumbs, the home tiles, the default favorites, the search, a card's
//link - skips it.
export function isNotFound( route:Route ):boolean{ return !!route.data?.['notFound']; }

//First match wins, so the duplicated 'access' path resolves like Angular's own matcher.  ':param' segments match anything;
//'**' consumes any remainder, so every prefix inside a node browse path is routable - but not the not-found catch-all.
export function matchConfig( routes:Routes, segments:string[] ):Route|undefined{
	for( const config of routes ){
		if( isNotFound(config) )
			continue;
		if( config.path=='**' )
			return config;
		const configSegments = (config.path ?? '').split('/').filter( s=>s.length );
		if( configSegments.length>segments.length || !configSegments.every( (cs,j)=>cs.startsWith(':') || cs==segments[j] ) )
			continue;
		if( configSegments.length==segments.length )
			return config;
		const child = config.children ? matchConfig( config.children, segments.slice(configSegments.length) ) : undefined;
		if( child )
			return child;
	}
	return undefined;
}

//How many of `segments` a matching route spells out literally - -1 when nothing matches.  ':param' routes accept anything, so
//when several candidate urls match ('/gateways/users/alice' fits gateways/:gateway/:connection) the one with the most literal
//segments is the one the writer meant ('/access/users/alice').
export function matchLiterals( routes:Routes, segments:string[] ):number{
	for( const config of routes ){
		if( isNotFound(config) )
			continue;
		if( config.path=='**' )
			return 0;
		const configSegments = (config.path ?? '').split('/').filter( s=>s.length );
		if( configSegments.length>segments.length || !configSegments.every( (cs,j)=>cs.startsWith(':') || cs==segments[j] ) )
			continue;
		const literals = configSegments.filter( cs=>!cs.startsWith(':') ).length;
		if( configSegments.length==segments.length )
			return literals;
		const child = config.children ? matchLiterals( config.children, segments.slice(configSegments.length) ) : -1;
		if( child>=0 )
			return literals+child;
	}
	return -1;
}

//A crumb for a url segment no route titles and no RouteStore entry names:  "apps/gateways/OpcHub.debug" has a page at
//"apps" and at the instance but none at "gateways", so the navbar fell back to the raw segment and showed it lowercase
//beside "Applications".  Url segments are camelCase identifiers by convention (AppResolver derives them from the
//program name - OpcGateway -> gateways, AppServer -> appServers), so a segment shaped like one is shown as words.
//Anything else - an instance name ("OpcHub.debug"), a slug with a dash, a number - is what the user typed and stays.
export function segmentDisplay( segment:string ):string{
	const decoded = decodeURIComponent( segment );
	if( !/^[a-z][a-z0-9]*(?:[A-Z][a-z0-9]*)*$/.test(decoded) )
		return decoded;
	return decoded.replace( /([A-Z])/g, ' $1' ).replace( /^[a-z]/, (c)=>c.toUpperCase() );
}
