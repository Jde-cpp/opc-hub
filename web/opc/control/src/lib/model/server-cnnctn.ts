import { ISlugRow, Mutation, MutationType, SlugRow, SlugRowProps  } from "jde-framework";
import { ceilingsError, ceilingsText, composeDriver, composeOpenProtocolUrl, hostError, kindFlow, kindOpenProtocol, kindUa, openProtocolScheme, parseCeilings, parseDriver, parseOpenProtocolUrl, portError, timeZoneError } from "./open-protocol";
import { Server } from "./server";

export type CnnctnSlug = string;
//The spec's connection settings (docs/clients/spec.md *Connection settings*), each null for the gateway's own rule or setting.
//The modes are UA's wire names, the policies full uris - what the gateway's `security_mode` and `security_policy` hold and refuse
//anything else for.  The durations are milliseconds, as UA's Duration is.
export const securityModes = ["None", "Sign", "SignAndEncrypt"] as const;
export type SecurityMode = typeof securityModes[number];
export const securityPolicyNone = "http://opcfoundation.org/UA/SecurityPolicy#None";
//the secured policies the gateway carries (ClientSecurity::SecuredPolicyUris), strongest last as open62541 ranks them
export const securedPolicies = ["Basic256Sha256", "Aes128_Sha256_RsaOaep", "Aes256_Sha256_RsaPss"].map( n=>`http://opcfoundation.org/UA/SecurityPolicy#${n}` );
export const connectionSettingNames = ["securityMode", "securityPolicy", "sessionTimeout", "requestTimeout", "pingInterval", "ttl", "locale"] as const;
export type ConnectionSettingName = typeof connectionSettingNames[number];
const durationNames = ["sessionTimeout", "requestTimeout", "pingInterval", "ttl"] as const;
//the most a duration takes, in milliseconds:  SQL Server's int, about 24 days (ServerCnnctn::MaxDurationMs)
export const maxDurationMs = 2147483647;
//The kinds the form offers (docs/open-protocol/spec.md *The row*):  a UA server at a url, or a tightening controller the gateway
//serves through a driver's UA server of its own.  `flow` is the flow server's one row, which the gateway makes and keeps.
export const kinds = [kindUa, kindOpenProtocol] as const;
export const kindLabels:Record<string,string> = { [kindUa]: "OPC UA", [kindOpenProtocol]: "Open Protocol", [kindFlow]: "Flow server" };
//The Open Protocol form's fields, which the row holds beside the columns they compose (open-protocol.ts):  `host` and `port` the url,
//`timeZone` and `revisions`, the ceilings as one line, the `driver` column.
export const openProtocolFieldNames = ["host", "port", "timeZone", "revisions"] as const;
export type OpenProtocolFieldName = typeof openProtocolFieldNames[number];
//what a kind whose server is the gateway's own fixes, and the form hides:  the gateway supplies the first two and refuses a value for any (spec *The row*)
export const ownServerColumns = ["url", "certificateUri", "defaultBrowseNs", "securityMode", "securityPolicy"] as const;

export class ServerCnnctn extends SlugRow<ServerCnnctn>{
	constructor( obj:ServerCnnctnProps ){
		super(ServerCnnctn.typeName, obj);
		this.url = obj.url;
		this.certificateUri = obj.certificateUri;
		this.defaultBrowseNs = ServerCnnctn.toNs( obj.defaultBrowseNs );
		this.securityMode = ServerCnnctn.toText( obj.securityMode );
		this.securityPolicy = ServerCnnctn.toText( obj.securityPolicy );
		this.sessionTimeout = ServerCnnctn.toMs( obj.sessionTimeout );
		this.requestTimeout = ServerCnnctn.toMs( obj.requestTimeout );
		this.pingInterval = ServerCnnctn.toMs( obj.pingInterval );
		this.ttl = ServerCnnctn.toMs( obj.ttl );
		this.locale = ServerCnnctn.toText( obj.locale );
		this.kind = ServerCnnctn.toKind( obj.kind );
		this.driver = ServerCnnctn.toText( obj.driver );
		//the form's fields where the record has them - an edited copy - else read from the columns, a row as the gateway answers it
		const url = parseOpenProtocolUrl( this.url ), driver = parseDriver( this.driver );
		this.host = obj.host ?? url.host;
		this.port = obj.port ?? url.port;
		this.timeZone = obj.timeZone ?? driver.timeZone ?? "";
		this.revisions = obj.revisions ?? ceilingsText( driver.revisions );
		this.server = obj.server;
		this.serverError = obj.serverError;
	}

	override get canSave():boolean{ return super.canSave && this.connectable; }//url is non-null in the gateway meta - the base only knows name/slug
	//What a connect needs and the gateway would save:  a url, an Open Protocol row's a host, a slug that passes the insert's rule, since
	//it names the certificate the gateway presents and the credential this session holds, and settings the gateway would not refuse.
	//Test Connection's gate, and Save's with a name.
	get connectable():boolean{
		return !!this.effectiveUrl && !!this.slug?.trim() && !this.fieldError("slug") && !this.fieldError("url") && !connectionSettingNames.some( n=>this.fieldError(n) )
			&& !(this.isOpenProtocol && (!this.host?.trim() || openProtocolFieldNames.some( n=>this.fieldError(n) )));
	}
	//reviews/install-issues.md #48:  the slug is the `slug\user` login's prefix, a certificate's file name (OpcHub.<slug>.pem) and a
	//url segment, and " eng-test" went into all three.  Trimmed on the way out (mutation); what is left must match the gateway's
	//own rule, which refuses anything else (ServerCnnctnMAwait, at the create).
	static slugPattern = /^[A-Za-z0-9][A-Za-z0-9._-]*$/;
	//the gateway's rule for a slug that names a file or a directory (FileSlugError):  a connection's, and a flow's
	static fileSlugError( slug:string ):string|undefined{ return ServerCnnctn.slugPattern.test( slug ) ? undefined : "Letters, digits, '.', '_' and '-' only, starting with a letter or digit."; }
	//the url segments of the lists under a gateway's page, ahead of ':connection' in app.routes.ts, which would shadow a connection's
	//page:  the watch list (419-watch #12), the historian's (#337), the calendars (#391), OEE's (#404), its dashboard (#406) and the
	//flows (#455).  As ServerCnnctn::SlugError.
	static readonly reservedSlugs = ['watch', 'templates', 'groups', 'calendars', 'machines', 'lines', 'reasons', 'oee', 'flows'];
	//The gateway's save rules on the settings (ServerCnnctn::SettingsError), said under the field before Save tries them:  Sign or
	//Sign & Encrypt, by the mode or by a secured policy, needs the certificate URI that puts the gateway's certificate on the channel;
	//a policy of None and a secured channel, or a secured policy and a mode of None, contradict;  0 is the default only for the ping
	//interval, and no duration takes more than maxDurationMs.  And the kind's rules (ServerCnnctn::KindError):  a UA row does not dial an
	//Open Protocol controller's url, and an Open Protocol row's host, port, zone and ceilings are what OpenProtocolRow takes;  the mode
	//and policy are not a driver row's to set, so a value left on the record from its days as a UA row says nothing.
	override fieldError( field:string ):string|undefined{
		const slug = this.slug?.trim();
		if( field=="slug" )
			return (slug ? ServerCnnctn.fileSlugError( slug ) : undefined)
				?? (ServerCnnctn.reservedSlugs.includes( slug ) ? "Taken by a list on the gateway's page, whose url segment it is." : undefined);
		if( field=="url" )
			return !this.ownServer && this.url?.trim().startsWith( openProtocolScheme ) ? "An Open Protocol controller's url:  set the Kind to Open Protocol." : undefined;
		if( field=="host" )
			return hostError( this.host ?? "" );
		if( field=="port" )
			return portError( this.port );
		if( field=="timeZone" )
			return timeZoneError( this.timeZone );
		if( field=="revisions" )
			return ceilingsError( this.revisions );
		if( (durationNames as readonly string[]).includes(field) ){
			const ms = ServerCnnctn.toMs( this[field as ConnectionSettingName] );
			return ms===0 && field!="pingInterval" ? "0 is not allowed:  leave it empty for the default."
				: ms!==null && ms>maxDurationMs ? `At most ${maxDurationMs}, about 24 days.` : undefined;
		}
		if( this.ownServer )
			return undefined;
		const mode = ServerCnnctn.toText( this.securityMode ), policy = ServerCnnctn.toText( this.securityPolicy ), uri = this.certificateUri?.trim();
		const securedPolicy = !!policy && policy!=securityPolicyNone;
		if( field=="securityMode" ){
			if( (mode=="Sign" || mode=="SignAndEncrypt") && !uri )
				return "Needs a Certificate URI:  the server's application URI puts the gateway's certificate on the channel.";
			if( mode=="None" && securedPolicy )
				return "A secured policy needs Sign or Sign & Encrypt.";
		}
		if( field=="securityPolicy" ){
			if( securedPolicy && !uri && !mode )
				return "Needs a Certificate URI:  the server's application URI puts the gateway's certificate on the channel.";
			if( policy==securityPolicyNone && this.effectiveMode!="None" )
				return mode ? "Policy None takes mode None." : "Policy None takes mode None:  a Certificate URI means Sign & Encrypt unless the mode is set.";
		}
		return undefined;
	}
	//The channel's mode as the gateway reads the row:  the one set, else Sign & Encrypt with a Certificate URI and None without.
	get effectiveMode():SecurityMode{ return ServerCnnctn.toText( this.securityMode ) as SecurityMode || (this.certificateUri?.trim() ? "SignAndEncrypt" : "None"); }
	get isOpenProtocol():boolean{ return this.kind==kindOpenProtocol; }
	//a kind whose server is the gateway's own (ServerCnnctn::OwnServer):  the form shows no url, certificate URI, namespace, mode or policy
	get ownServer():boolean{ return this.kind==kindOpenProtocol || this.kind==kindFlow; }
	//The url the gateway stores:  an Open Protocol row's composed from the form's host and port, any other's as typed, trimmed.
	get effectiveUrl():string{ return this.isOpenProtocol ? composeOpenProtocolUrl( this.host ?? "", this.port ) : this.url?.trim() ?? ""; }
	//The `driver` the gateway stores:  an Open Protocol row's zone and ceilings as one canonical json text, null with neither;  null for
	//any other kind, which takes none.  Ceilings that do not parse count as none - fieldError holds the save until they do.
	get effectiveDriver():string|null{ return this.isOpenProtocol ? composeDriver( this.timeZone, parseCeilings(this.revisions) ?? {} ) : null; }

	override equals( row:ISlugRow ):boolean{
		let other = row as ServerCnnctn;
		return super.equals(row) && this.kind==other.kind && this.effectiveUrl==other.effectiveUrl && this.effectiveDriver==other.effectiveDriver
			&& this.certificateUri?.trim()==other.certificateUri?.trim() && ServerCnnctn.toNs(this.defaultBrowseNs)==ServerCnnctn.toNs(other.defaultBrowseNs)//the edited copy still holds the input's raw string
			&& connectionSettingNames.every( n=>ServerCnnctn.setting(this, n)===ServerCnnctn.setting(other, n) );
	}

	override mutation( original:ServerCnnctn ):Mutation[]{
		console.assert( this.canSave );
		let args = super.mutationArgs( original );
		if( args.slug!==undefined )
			args.slug = args.slug.trim();
		//A create states the kind, as it does the namespace;  an update names it where it changed.  The gateway writes null to what a
		//driver's kind fixes when a row changes to one (ServerCnnctnMAwait), so nothing of the four is sent for it.
		const kindChanged = this.kind!=ServerCnnctn.toKind( original?.kind );
		if( !original?.id || kindChanged )
			args["kind"] = this.kind;
		//trimmed, as Test Connection sends them:  `urn:plc ` passed the test and was saved as is, which the gateway's endpoint filter reads as urn:plc%20.
		const url = this.effectiveUrl, certificateUri = this.certificateUri?.trim();
		if( url!=original?.url?.trim() )
			args["url"] = url;
		//the stored text may not be canonical - one written by hand - so the comparison is over the canonical form of both
		const driver = this.effectiveDriver;
		if( driver!=ServerCnnctn.canonicalDriver( original?.driver ) )
			args["driver"] = driver;
		if( !this.ownServer ){
			if( certificateUri!=original?.certificateUri )
				args["certificateUri"] = certificateUri;
			//A create always states it:  left at the default the form shows, it used to send nothing and store NULL, which the gateway
			//read as another namespace (reviews/m3-closing.md #3) - a connection should not depend on what NULL means.  Through toNs,
			//as equals() compares:  Properties.onChange leaves the input's raw string on the record.  A row back from a driver's kind,
			//whose namespace the gateway nulled, states it again for the same reason.
			const ns = ServerCnnctn.toNs( this.defaultBrowseNs );
			if( !original?.id || kindChanged || ns!=ServerCnnctn.toNs(original.defaultBrowseNs) )
				args["defaultBrowseNs"] = ns;
		}
		//the settings, each as the form holds it:  null clears a saved value, and a create leaves an unset one out
		for( const name of connectionSettingNames ){
			if( this.ownServer && (name=="securityMode" || name=="securityPolicy") )
				continue;
			const value = ServerCnnctn.setting( this, name );
			if( original?.id ? value!==ServerCnnctn.setting(original, name) : value!==null )
				(args as any)[name] = value;
		}
		return Object.keys( args ).length ? [new Mutation(this.type, this.id, args, original?.id ? MutationType.Update : MutationType.Create)] : [];
	}

	//The properties form writes the text input's string straight onto the record, so anything that reads the field goes
	//through here:  the constructor keeps it a number (the mutation must send `defaultBrowseNs:2`, not `"2"`), and a
	//cleared/non-numeric/0 entry falls back to the default rather than being saved as garbage.
	static toNs( value:unknown ):number{ return Number(value) || 1; }
	//A duration as the gateway stores it:  a whole number of milliseconds, or null for the gateway's setting - the number input's
	//empty string, a blank, and anything that is no number all mean unset.
	static toMs( value:unknown ):number|null{
		if( value===null || value===undefined || value==="" )
			return null;
		const n = Number( value );
		return Number.isFinite( n ) && n>=0 ? Math.floor( n ) : null;
	}
	static toText( value:unknown ):string|null{ const s = typeof value=="string" ? value.trim() : value==null ? "" : String( value ); return s || null; }
	//the kind as the gateway reads the column:  null, the rows before the column, is ua
	static toKind( value:unknown ):string{ return ServerCnnctn.toText( value ) ?? kindUa; }
	//a stored `driver` in the form the row composes, for a comparison with it
	static canonicalDriver( text:string|null|undefined ):string|null{ const d = parseDriver( text ); return composeDriver( d.timeZone, d.revisions ); }
	/** The setting as the gateway would read the form's value:  a trimmed string or null, a whole number of milliseconds or null.  A
	 * kind whose server is the gateway's own has no mode or policy of its own:  null, whatever the record holds from its days as a UA row. */
	static setting( row:Partial<ServerCnnctn>, name:ConnectionSettingName ):string|number|null{
		if( (name=="securityMode" || name=="securityPolicy") && ServerCnnctn.ownServerKind( row.kind ) )
			return null;
		return (durationNames as readonly string[]).includes( name ) ? ServerCnnctn.toMs( row[name] ) : ServerCnnctn.toText( row[name] );
	}
	static ownServerKind( kind:unknown ):boolean{ const k = ServerCnnctn.toKind( kind ); return k==kindOpenProtocol || k==kindFlow; }
	/** The settings as `testConnection` takes them - what Save would send, null where the form leaves one unset. */
	static settings( row:Partial<ServerCnnctn> ):Record<ConnectionSettingName,string|number|null>{
		return Object.fromEntries( connectionSettingNames.map( n=>[n, ServerCnnctn.setting(row, n)] ) ) as Record<ConnectionSettingName,string|number|null>;
	}
	get properties():ServerCnnctn{ let properties = new ServerCnnctn(this as ServerCnnctnProps); return properties; }
	url:string;
	certificateUri:string;
	defaultBrowseNs:number=1;
	securityMode:string|null;
	securityPolicy:string|null;
	sessionTimeout:number|null;
	requestTimeout:number|null;
	pingInterval:number|null;
	ttl:number|null;
	locale:string|null;
	kind:string;//kindUa, kindOpenProtocol or kindFlow
	driver:string|null;//the column's json text, as the gateway answered it;  the form edits the four below, which effectiveDriver composes
	host:string;
	port:string;//as the number input holds it;  "" is 4545
	timeZone:string;//"" is the gateway's own zone
	revisions:string;//the ceilings as one line, `0061:3 0001:6`
	server:Server;
	serverError?:string;//the resolver's connect failure, when `server` is unset for an existing row - the Connection tab shows it
	static typeName = "ServerConnection";
}
export type ServerCnnctnProps = SlugRowProps & { url:string; certificateUri:string; defaultBrowseNs?:number; kind?:string|null; driver?:string|null; server:Server; serverError?:string }
	& Partial<Record<ConnectionSettingName,string|number|null>> & Partial<Record<OpenProtocolFieldName,string>>;