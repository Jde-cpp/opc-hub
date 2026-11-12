import { AppService, CalendarRef, CalendarService } from 'jde-framework';

//The application server's shift calendars as a machine's and a line's form pick from them (#404, #409), with the page they are
//a tab of, under which each has its own:  a hub's own page, else the application server's, found by its registration.
export type OeeCalendars = { calendars:CalendarRef[]; url?:string[] };
export async function readOeeCalendars( app:AppService, service:CalendarService, gatewaySlug:string ):Promise<OeeCalendars>{
	const [calendars, registrations] = await Promise.all( [service.calendars( app ), app.registrations()] );
	const registered = ( program:string, instanceName?:string )=>registrations.find( r=>r.programName==program && (instanceName===undefined || r.instanceName==instanceName) );
	const hub = !registered( 'OpcGateway', gatewaySlug ) && !!registered( 'OpcHub', gatewaySlug );//as GatewayDetail.isHub
	const appServer = registered( 'AppServer' )?.instanceName;
	return { calendars, url: hub ? ['/apps/gateways', gatewaySlug, 'calendars'] : appServer ? ['/apps/appServers', appServer, 'calendars'] : undefined };
}