import { ISlugRow, Mutation, MutationType, SlugRow, SlugRowProps  } from "jde-framework";
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
		this.server = obj.server;
		this.serverError = obj.serverError;
	}

	override get canSave():boolean{ return super.canSave && !!this.url?.trim() && !!this.slug.trim() && !this.fieldError("slug") && !this.fieldError("securityMode") && !this.fieldError("securityPolicy"); }//url is non-null in the gateway meta - the base only knows name/slug
	//reviews/install-issues.md #48:  the slug is the `slug\user` login's prefix, a certificate's file name (OpcHub.<slug>.pem) and a
	//url segment, and " eng-test" went into all three.  Trimmed on the way out (mutation); what is left must match the gateway's
	//own rule, which refuses anything else (OpcQLHook::InsertBefore).
	static slugPattern = /^[A-Za-z0-9][A-Za-z0-9._-]*$/;
	//The gateway's save rules on the settings (ServerCnnctn::SettingsError), said under the field before Save tries them:  Sign or
	//Sign & Encrypt, by the mode or by a secured policy, needs the certificate URI that puts the gateway's certificate on the channel;
	//a policy of None and a secured channel, or a secured policy and a mode of None, contradict.
	override fieldError( field:string ):string|undefined{
		const slug = this.slug?.trim();
		if( field=="slug" )
			return slug && !ServerCnnctn.slugPattern.test( slug ) ? "Letters, digits, '.', '_' and '-' only, starting with a letter or digit." : undefined;
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

	override equals( row:ISlugRow ):boolean{
		let other = row as ServerCnnctn;
		return super.equals(row) && this.url?.trim()==other.url?.trim() && this.certificateUri?.trim()==other.certificateUri?.trim() && ServerCnnctn.toNs(this.defaultBrowseNs)==ServerCnnctn.toNs(other.defaultBrowseNs)//the edited copy still holds the input's raw string
			&& connectionSettingNames.every( n=>ServerCnnctn.setting(this, n)===ServerCnnctn.setting(other, n) );
	}

	override mutation( original:ServerCnnctn ):Mutation[]{
		console.assert( this.canSave );
		let args = super.mutationArgs( original );
		if( args.slug!==undefined )
			args.slug = args.slug.trim();
		//trimmed, as Test Connection sends them:  `urn:plc ` passed the test and was saved as is, which the gateway's endpoint filter reads as urn:plc%20.
		const url = this.url?.trim(), certificateUri = this.certificateUri?.trim();
		if( url!=original?.url )
			args["url"] = url;
		if( certificateUri!=original?.certificateUri )
			args["certificateUri"] = certificateUri;
		//A create always states it:  left at the default the form shows, it used to send nothing and store NULL, which the gateway
		//read as another namespace (reviews/m3-closing.md #3) - a connection should not depend on what NULL means.  Through toNs,
		//as equals() compares:  Properties.onChange leaves the input's raw string on the record.
		const ns = ServerCnnctn.toNs( this.defaultBrowseNs );
		if( !original?.id || ns!=ServerCnnctn.toNs(original.defaultBrowseNs) )
			args["defaultBrowseNs"] = ns;
		//the settings, each as the form holds it:  null clears a saved value, and a create leaves an unset one out
		for( const name of connectionSettingNames ){
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
	/** The setting as the gateway would read the form's value:  a trimmed string or null, a whole number of milliseconds or null. */
	static setting( row:Partial<ServerCnnctn>, name:ConnectionSettingName ):string|number|null{
		return (durationNames as readonly string[]).includes( name ) ? ServerCnnctn.toMs( row[name] ) : ServerCnnctn.toText( row[name] );
	}
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
	server:Server;
	serverError?:string;//the resolver's connect failure, when `server` is unset for an existing row - the Connection tab shows it
	static typeName = "ServerConnection";
}
export type ServerCnnctnProps = SlugRowProps & { url:string; certificateUri:string; defaultBrowseNs?:number; server:Server; serverError?:string } & Partial<Record<ConnectionSettingName,string|number|null>>;