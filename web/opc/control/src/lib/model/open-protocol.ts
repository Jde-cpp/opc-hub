//An Open Protocol connection as the form holds it (docs/open-protocol/spec.md *The row*, *Web*):  the url's host and port, the
//`driver` column's time zone and revision ceilings, what MID 0002 says of the controller, and the driver's `_Op` diagnostics.  The
//rules mirror the gateway's OpenProtocolRow, which refuses at the save what the form says under the field.
import { reading } from './reading';

export const kindUa = "ua", kindOpenProtocol = "open-protocol", kindFlow = "flow";//what `kind` holds (ServerCnnctn::KindUa, …);  null is ua
export const openProtocolScheme = "open-protocol://";
export const openProtocolDefaultPort = 4545;//where the url names none (§2.1.1)

//The url's host and port as text:  a url of another scheme has no host, and a url with no port has the default, so the form shows it.
export const parseOpenProtocolUrl = ( url:string|null|undefined ):{ host:string; port:string }=>{
	const u = url?.trim() ?? "";
	if( !u.startsWith(openProtocolScheme) )
		return { host: "", port: String(openProtocolDefaultPort) };
	const m = /^(.*?)(?::(\d*))?$/.exec( u.substring(openProtocolScheme.length) )!;//the last `:digits` is the port, so an IPv6 host in brackets keeps its colons
	return { host: m[1], port: m[2] ? m[2] : String(openProtocolDefaultPort) };
};
//`open-protocol://host:port`, the port stated, 4545 where the form leaves it, so a saved url and the form's compare as one.
export const composeOpenProtocolUrl = ( host:string, port:string|number|null|undefined ):string=>{
	const p = String( port ?? "" ).trim();
	return `${openProtocolScheme}${host.trim()}:${p || openProtocolDefaultPort}`;
};
//The gateway's rule (OpenProtocolRow's readUrl):  a name or an address of letters, digits, '.', '-' and '_', or an IPv6 address in
//brackets, which may hold ':' and a zone's '%' besides, with nothing after the ']'.  Empty says nothing - the required marker covers
//it - and will not save.
export const hostError = ( host:string ):string|undefined=>{
	const h = host.trim();
	if( !h )
		return undefined;
	if( h.startsWith("[") )
		return /^\[[A-Za-z0-9._:%-]+\]$/.test( h ) ? undefined : "An IPv6 address in brackets, [::1] or [fe80::1%eth0].";
	if( /^[A-Za-z0-9._-]+$/.test(h) )
		return undefined;
	return h.includes(":") ? "An IPv6 address goes in brackets, [::1]." : "A host name or address:  letters, digits, '.', '-' and '_'.";
};
export const portError = ( port:string|number|null|undefined ):string|undefined=>{
	const p = String( port ?? "" ).trim();
	if( !p )
		return undefined;//4545
	const n = Number( p );
	return /^\d+$/.test( p ) && n>=1 && n<=65535 ? undefined : "A port from 1 to 65535.";
};

//`driver`:  {"timeZone":"Europe/Stockholm","revisions":{"0061":3}}, either member left out where unset;  null where both are.
export type OpenProtocolDriver = { timeZone:string|null; revisions:Record<string,number> };
export const parseDriver = ( text:string|null|undefined ):OpenProtocolDriver=>{
	const none:OpenProtocolDriver = { timeZone: null, revisions: {} };
	if( !text?.trim() )
		return none;
	try{
		const o = JSON.parse( text );
		if( !o || typeof o!='object' || Array.isArray(o) )
			return none;
		const zone = typeof o.timeZone=='string' && o.timeZone.trim() ? o.timeZone.trim() : null;
		const revisions:Record<string,number> = {};
		if( o.revisions && typeof o.revisions=='object' && !Array.isArray(o.revisions) ){
			for( const [mid, revision] of Object.entries(o.revisions) ){
				if( typeof revision=='number' )
					revisions[mid] = revision;
			}
		}
		return { timeZone: zone, revisions };
	}
	catch{
		return none;
	}
};
//The canonical text - members in one order, MIDs sorted - so what the form composes and what the gateway stored compare as strings.
export const composeDriver = ( timeZone:string|null|undefined, revisions:Record<string,number> ):string|null=>{
	const zone = timeZone?.trim();
	const mids = Object.keys( revisions ).sort();
	if( !zone && !mids.length )
		return null;
	const o:Record<string,unknown> = {};
	if( zone )
		o["timeZone"] = zone;
	if( mids.length )
		o["revisions"] = Object.fromEntries( mids.map( m=>[m, revisions[m]] ) );
	return JSON.stringify( o );
};

//The ceilings as one line, `0061:3 0001:6`, the spelling `_Op.Revisions` shows them in:  pairs of a four-digit MID and a revision 1 to
//999, what the header's three digits hold, separated by spaces or commas.  undefined for text that is not that.
export const parseCeilings = ( text:string|null|undefined ):Record<string,number>|undefined=>{
	const revisions:Record<string,number> = {};
	for( const pair of (text ?? "").split( /[\s,]+/ ).filter( x=>x ) ){
		const m = /^(\d{4}):(\d{1,3})$/.exec( pair );
		if( !m || +m[2]<1 )
			return undefined;
		revisions[m[1]] = +m[2];
	}
	return revisions;
};
export const ceilingsText = ( revisions:Record<string,number> ):string=>Object.keys( revisions ).sort().map( m=>`${m}:${revisions[m]}` ).join( " " );
export const ceilingsError = ( text:string|null|undefined ):string|undefined=>parseCeilings( text ) ? undefined : "MID:revision pairs separated by spaces, 0061:3 - a four-digit MID and a revision from 1 to 999.";

//An IANA zone name as the gateway resolves it (std::chrono::locate_zone), which is case-sensitive:  a name the browser takes in another
//case is refused with the database's spelling.  The browser resolves a renamed zone to CLDR's older name, Asia/Kolkata to Asia/Calcutta,
//and the gateway's database links the two, so a name that resolves to another passes, and the gateway's refusal is the final word on it,
//as the calendar page has it.  Empty is the gateway's own zone.
export const timeZoneError = ( zone:string|null|undefined ):string|undefined=>{
	const z = zone?.trim();
	if( !z )
		return undefined;
	try{
		const resolved = new Intl.DateTimeFormat( undefined, {timeZone: z} ).resolvedOptions().timeZone;
		return resolved!=z && resolved.toLowerCase()==z.toLowerCase() ? `Not a zone name:  the database spells it ${resolved}.` : undefined;
	}
	catch{
		return "Not a zone the time zone database names, such as Europe/Stockholm.";
	}
};
//The zones the browser knows, for the form's suggestions;  none where it cannot list them.
export const timeZones = ():string[]=>{
	try{
		return (Intl as any).supportedValuesOf?.( 'timeZone' ) ?? [];
	}
	catch{
		return [];
	}
};

//What MID 0002 answered, as testConnection reports it and `_Op.Identity` holds it (Open Protocol Tables 13 to 18), each null where the
//revision it came at carries none.
export type OpenProtocolIdentity = {
	controllerName:string|null; supplierCode:string|null; openProtocolVersion:string|null; controllerSoftwareVersion:string|null; toolSoftwareVersion:string|null;
	controllerSerialNumber:string|null; systemType:number|null; systemSubtype:number|null; stationId:number|null; stationName:string|null
};
export const identityColumns = "controllerName supplierCode openProtocolVersion controllerSoftwareVersion toolSoftwareVersion controllerSerialNumber systemType systemSubtype stationId stationName";
//MID 0002 revision 5's system types (Table 17);  another number shows as it came
export const systemTypeNames:Record<number,string> = { 1: "Power Focus 4000", 2: "Power MACS 4000", 3: "Power Focus 6000", 4: "Micro Torque Focus 6000" };
export const systemTypeText = ( type:number|null|undefined, subtype?:number|null ):string=>{
	if( type==null )
		return "";
	const name = systemTypeNames[type] ?? String( type );
	return subtype==null ? name : `${name}, subtype ${subtype}`;
};
export type LabelValue = { label:string; value:string };
//The identity as the Connection tab lists it, a row per field whether or not the revision carried it
export const identityFields = ( id:OpenProtocolIdentity ):LabelValue[]=>{
	const text = ( v:unknown )=>v==null ? "" : String( v );
	return [
		{ label: "Controller", value: text( id.controllerName ) },
		{ label: "System", value: systemTypeText( id.systemType, id.systemSubtype ) },
		{ label: "Supplier Code", value: text( id.supplierCode ) },
		{ label: "Open Protocol Version", value: text( id.openProtocolVersion ) },
		{ label: "Controller Software", value: text( id.controllerSoftwareVersion ) },
		{ label: "Tool Software", value: text( id.toolSoftwareVersion ) },
		{ label: "Serial Number", value: text( id.controllerSerialNumber ) },
		{ label: "Station", value: id.stationId==null && !id.stationName ? "" : [id.stationName, id.stationId==null ? "" : `(${id.stationId})`].filter( x=>x ).join( " " ) }
	];
};
//`_Op`, the driver's diagnostics (spec *Address space*):  each a variable of the row's namespace, which is 2 on a driver server, UA's
//and the server's own before it (ServerCnnctn::DriverBrowseNs), at the string id of its path.  Read through nodes( opc, id ){ id value }.
export const driverBrowseNs = 2;
export const opNames = ["Connected", "Since", "Url", "LastError", "Reconnects", "Identity", "Revisions", "LinkLevel", "Results", "LostResults", "Skew"] as const;
export type OpName = typeof opNames[number];
export const opIds = opNames.map( name=>({ns: driverBrowseNs, s: `_Op.${name}`}) );
export const opQuery = "nodes( opc:$opc, id:$id ){ id value }";
export type OpRow = { id?:{ns?:number; s?:string}; value?:unknown };
//a read of one variable:  its value, or the status it reads instead of one
export type OpReading = { value?:unknown; sc?:number };
export type OpDiagnostics = Partial<Record<OpName,OpReading>>;
//The rows by name:  a row the gateway did not answer is absent, and the tab says so
export const readOp = ( rows:OpRow[]|null|undefined ):OpDiagnostics=>{
	const y:OpDiagnostics = {};
	for( const name of opNames ){
		const row = rows?.find( r=>r.id?.s==`_Op.${name}` );
		if( row && "value" in row )
			y[name] = reading( row.value );
	}
	return y;
};
//The two statuses `_Op` reads before a value comes (spec *Address space*), in words;  any other by the app's status text
export const scBadWaitingForInitialData = 0x80320000, scBadNotSupported = 0x803D0000, scBadNoCommunication = 0x80310000;