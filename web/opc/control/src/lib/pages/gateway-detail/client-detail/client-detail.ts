import { LiveAnnouncer } from '@angular/cdk/a11y';
import { Component, computed, inject, signal, TemplateRef, viewChild } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatTabsModule } from '@angular/material/tabs';

import { DetailActions, DetailPage, errorMessage, isRefusal, PickOption, Properties} from 'jde-framework';
import { Latest } from 'jde-spa';

import { DiscoverDialog } from './discover-dialog';
import { OpenProtocolFields } from './open-protocol-fields/open-protocol-fields';
import { ServerDiagnostics } from './server-diagnostics/server-diagnostics';
import { ServerProperties } from './server-properties/server-properties';
import { ConnectionSettingName, connectionSettingNames, kindLabels, kinds, ownServerColumns, securedPolicies, securityModes, securityPolicyNone, ServerCnnctn, ServerCnnctnProps } from '../../../model/server-cnnctn';
import { EndpointPick } from '../../../model/discovery';
import { modeName, policyName } from '../../../model/gateway-endpoint';
import { ceilingsText, identityColumns, kindFlow, kindOpenProtocol, OpDiagnostics, OpenProtocolIdentity, opIds, opQuery, OpRow, parseDriver, readOp } from '../../../model/open-protocol';
import { Gateway } from '../../../services/gateway-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';
import { ConnectionCertificates, connectionCertificatesQuery, namespaceColumns, Server, serverDescColumns, TestConnectionResult, trustServerMutation } from '../../../model/server';
import { OpcStore } from '../../../services/opc-store';

//what a connect depends on, as the test and the save send it:  the kind and its driver settings, the three the certificate and the
//credential key on, and the spec's connection settings (docs/clients/spec.md *Connection settings*), null where the form leaves one
//to the gateway.  An Open Protocol row's url is the one its host and port compose, and it has no certificate URI, mode or policy of its
//own (docs/open-protocol/spec.md *The row*).
type ConnectSettings = { slug:string; url:string; kind:string; driver:string|null; certificateUri:string } & Record<ConnectionSettingName,string|number|null>;
const connectSettings = ( p:Partial<ServerCnnctn> ):ConnectSettings=>({
	slug: p.slug?.trim() ?? "", url: p.effectiveUrl ?? p.url?.trim() ?? "", kind: ServerCnnctn.toKind( p.kind ), driver: p.effectiveDriver ?? null,
	certificateUri: ServerCnnctn.ownServerKind( p.kind ) ? "" : p.certificateUri?.trim() ?? "", ...ServerCnnctn.settings(p)
});
const settingLabels:Record<string,string> = { kind: "Kind", url: "URL", certificateUri: "Certificate URI", defaultBrowseNs: "Default Namespace", securityMode: "Security Mode", securityPolicy: "Security Policy", sessionTimeout: "Session Timeout", requestTimeout: "Request Timeout", pingInterval: "Ping Interval", ttl: "Idle TTL", locale: "Locales" };

@Component( {
	templateUrl: './client-detail.html',
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves
		//the class name out, so four routed pages that now share DetailPage and this host string could collide with NG0912.
		host: {class:'main-content my-content client-detail'},
	imports: [DetailActions, MatButtonModule, MatTabsModule, OpenProtocolFields, Properties, ServerDiagnostics, ServerProperties],
})
export class ClientDetail extends DetailPage<ServerCnnctn>{
	constructor(){ super( 'client-detail' ); }

	protected override get ctor(){ return ServerCnnctn; }
	protected override onRow(){//no child collections - the connection is its properties;  the Connection tab opens on the resolver's describe, until a test replaces it
		this.gateway = (<GatewayDetailData<ServerCnnctn>>this.pageData).gateway;
		this.server.set( this.row.server );
		this.serverError.set( this.row.serverError );
		this.identity.set( undefined );
		this.#resultFor.set( this.row.server || this.row.serverError ? connectSettings(this.row) : undefined );
		this.testing.set( undefined );
		this.trusting.set( false );
		this.certificates.set( undefined );
		this.#rowLoads.next();
		this.#loadCertificates();//the rejected server certificate and the gateway's own, read from the row (no connect)
		this.#loadOp();//an Open Protocol row's diagnostics, read from its driver server
	}
	protected override upsert():ServerCnnctn{
		return new ServerCnnctn( {
			...this.properties(),
		} as ServerCnnctnProps);
	}
	protected override afterMutate(){//the node pages answer from OpcStore's describe:  an edit has to reach them, a delete end them (reviews/m3-closing.md #5)
		if( this.row.slug )
			this.#opcStore.forget( this.gateway.slug, this.row.slug );
	}
	protected override get title(){ return this.row.name ? `${this.row.name} - Connection` : "New Connection"; }
	override get ql(){ return this.gateway; }//per-gateway, not a single injected service - the row's, from ClientResolver

	//what a user reads first, then what the connection is, then the connection, then the two technical fields, then the spec's settings
	//in the order the channel is built:  the mode and policy, the session's timeouts, the gateway's own keep-alive and TTL, the locales
	//- the alphabetical default buried Description between them and put Url last.  An Open Protocol row's own fields follow the kind
	//(`slots`), in place of the url and the certificate URI.
	readonly fieldOrder = ["slug", "name", "description", "kind", "url", "certificateUri", "defaultBrowseNs", ...connectionSettingNames];
	readonly fieldLabels = settingLabels;//the camelCase split gives "Url", "Certificate Uri", "Default Browse Ns", "Ttl"
	//The kind the form holds:  the rest of the form follows it, a UA row's url and certificate URI or an Open Protocol row's fields
	readonly formKind = computed( ()=>ServerCnnctn.toKind( (this.properties() as Partial<ServerCnnctn>|null)?.kind ) );
	readonly formOpenProtocol = computed( ()=>this.formKind()==kindOpenProtocol );
	//`driver` is the column the Open Protocol fields compose, never typed;  a kind whose server is the gateway's own has no url,
	//certificate URI, namespace, mode or policy of its own (ServerCnnctn::OwnServer), so the form shows none
	readonly excludedColumns = computed<string[]>( ()=>ServerCnnctn.ownServerKind( this.formKind() ) ? ["driver", ...ownServerColumns] : ["driver"] );
	//the slug names the certificate and the credential, so a saved row keeps it;  the flow server's row is of its kind alone (ServerCnnctn::KindError)
	readonly readonlyFields = computed<string[]>( ()=>[...(this.isNew ? [] : ["slug"]), ...(this.row?.kind==kindFlow ? ["kind"] : [])] );
	openProtocolFields = viewChild<TemplateRef<unknown>>( 'openProtocolFields' );
	readonly slots = computed<Record<string,TemplateRef<unknown>>>( ()=>{
		const template = this.openProtocolFields();
		return template && this.formOpenProtocol() ? {kind: template} : {};
	} );
	//The kind, mode and policy picks, the unset option labelled with what unset means to the gateway:  the floor the Certificate URI sets
	//for the mode, and for the policy the strongest the two sides share.  The policies are the ones the gateway carries;  it refuses
	//any other at save.  Over the floor alone, whose string equality keeps a keystroke elsewhere from rebuilding the form's fields.
	//The kinds are the two the form offers;  the flow server's row shows its own, which it keeps.
	readonly #floor = computed( ()=>(this.properties() as Partial<ServerCnnctn>|null)?.certificateUri?.trim() ? "SignAndEncrypt" : "None" );
	readonly picks = computed<{ kind:PickOption[]; securityMode:PickOption[]; securityPolicy:PickOption[] }>( ()=>({
		kind: [ ...kinds.map( k=>({value: k, label: kindLabels[k]}) ), ...(this.row?.kind==kindFlow ? [{value: kindFlow, label: kindLabels[kindFlow]}] : []) ],
		securityMode: [ {value: null, label: `Default (${modeName(this.#floor())})`}, ...securityModes.map( m=>({value: m, label: modeName(m)}) ) ],
		securityPolicy: [ {value: null, label: "Default (strongest shared)"}, {value: securityPolicyNone, label: "None"}, ...securedPolicies.map( uri=>({value: uri, label: policyName(uri)}) ) ]
	}) );
	readonly units = { sessionTimeout: "ms", requestTimeout: "ms", pingInterval: "ms", ttl: "ms" };
	readonly hints = {
		kind: "OPC UA, a server at a URL;  Open Protocol, a tightening controller the gateway reads through a driver of its own.",
		sessionTimeout: "Empty:  the client library's 20 minutes.  The server may shorten it.",
		requestTimeout: "Empty:  the gateway's requestTimeout setting.  Also how long Test Connection waits.",
		pingInterval: "Empty:  the gateway's pingInterval setting, or half the session timeout where that is shorter.  0 turns the keep-alive off.",
		ttl: "Empty:  the gateway's ttl setting - how long an idle session is kept open.",
		locale: "Comma-separated locale ids, en-US,de.  Empty sends none."
	};
	get serverCnnctn(){ return this.row; }//the template's name for it

	//Test Connection:  the form's values, saved or not, connected by the gateway as the saved row would be (testConnection) and
	//shown in the Connection tab's states - a wrong url or an untrusted certificate says why there, and a saved connection whose
	//page loaded Not connected is retried without a reload.  The base's `!id` clamp still lands a new row on Properties.
	//Enabled on what the gateway needs - a url, and a slug that passes the insert's rule, since the slug names the certificate the
	//gateway presents and the credential this session holds, and settings the gateway would save - and never twice at once.
	//On an Open Protocol row the gateway speaks to the controller itself, MID 0001 to 0003, and answers its identity in place of a
	//server's description (docs/open-protocol/spec.md *The row*).
	readonly canTest = computed( ()=>{
		const p = this.properties() as ServerCnnctn|null;
		return !this.testing() && !!p?.connectable;
	} );
	async onTestClick(){
		if( !this.canTest() )
			return;//disabledInteractive:  the disabled button still takes clicks.
		const p = this.properties() as ServerCnnctn;
		const settings = connectSettings( p );
		const rowLoad = this.#rowLoads.current;
		this.testing.set( settings.url );
		this.tabIndex.set( 1 );//the result lands on the Connection tab, and it shows the connect in flight
		this.#announcer.announce( `Connecting to ${settings.url}.` );
		let server:Server|undefined, identity:OpenProtocolIdentity|undefined, error:string|undefined, refused:unknown;
		try{
			const { slug, ...vars } = settings;
			const data = await this.gateway.postQL<{testConnection:TestConnectionResult&OpenProtocolIdentity}>( ClientDetail.testMutation, {opc: slug, ...vars} );
			const result = data.testConnection;
			if( settings.kind==kindOpenProtocol )
				identity = result;
			else
				server = new Server({ connection: {...p, ...settings} as ServerCnnctnProps, desc: result, policy: result.securityPolicyUri, mode: result.securityMode, namespaces: result.namespaces });
		}
		catch( e ){//the tab's not-connected state carries the reason - no toast over it
			if( isRefusal(e) )
				refused = e;
			else
				error = errorMessage(e);
		}
		if( !this.#rowLoads.is(rowLoad) )
			return;//the router reuses this page:  the answer is for a connection no longer shown.
		this.testing.set( undefined );
		//testConnection needs Update, which a viewer lacks:  a refusal says nothing about the server, so the tab keeps what it showed
		//and the denial is a toast, as Save's is.
		if( refused ){
			this.snackbar.exception( "Test Connection was refused.", refused );
			return;
		}
		//the answer of the kind tested replaces the last:  a UA server's description, or a controller's identity, which sits above a saved
		//Open Protocol row's `_Op`, read again since the test does not touch the driver's session;  a failure says why, above `_Op` where shown
		this.server.set( server );
		this.identity.set( identity );
		this.serverError.set( error );
		this.#resultFor.set( settings );
		this.#announcer.announce( server ? `Connected to ${server.applicationName || settings.url}.` : identity ? `Connected to ${identity.controllerName || settings.url}.` : `Not connected:  ${error}` );
		await Promise.all( [this.#loadCertificates(), this.#loadOp( settings.kind==kindOpenProtocol )] );//the test connects under the slug, so a rejection records the server's certificate as a scheduled connect's does:  the Trust panel follows the test, without a reload;  and `_Op` follows the kind tested
	}
	//Discover (docs/clients/spec.md *Discovery*):  the servers the gateway finds at the form's url and the endpoints of the one
	//picked, in a dialog, and the endpoint chosen fills the form - the url, the Certificate URI with the server's applicationUri,
	//and the mode and policy - on the Properties tab, where it can be read before Test Connection or Save.  Nothing is saved.
	//Needs only a url:  the gateway's discovery client presents no certificate, so no slug names one.  A UA row's:  an Open Protocol
	//controller answers no discovery, and the flow server's row has none of the fields a pick fills - the form hides them for both kinds.
	readonly canDiscover = computed( ()=>!this.testing() && !ServerCnnctn.ownServerKind( this.formKind() ) && !!(this.properties() as Partial<ServerCnnctn>|null)?.url?.trim() );
	async onDiscoverClick(){
		if( !this.canDiscover() )
			return;//disabledInteractive, as Test Connection
		const url = (this.properties() as ServerCnnctn).url.trim();
		const rowLoad = this.#rowLoads.current;
		const pick = await DiscoverDialog.open( this.dialog, {gateway: this.gateway, url} );
		if( !pick || !this.#rowLoads.is(rowLoad) )
			return;
		this.applyPick( pick );
	}
	applyPick( pick:EndpointPick ){
		this.properties.set( Properties.edited( ServerCnnctn, this.properties(), pick ) );
		this.tabIndex.set( 0 );
		this.#announcer.announce( `URL, Certificate URI, Security Mode and Security Policy set from ${pick.url}.` );
	}
	//Trust (docs/clients/spec.md *Trust*, #415):  move the rejected server certificate into the gateway's trusted dir and
	//reconnect the slug.  trustServer needs Administer, which a viewer lacks:  a refusal is a toast, as Test Connection's is, and
	//the panel keeps the certificate.  On success the certificate is trusted - the panel clears - and the connection is re-described.
	async onTrust( thumbprint:string ){
		const slug = this.row.slug;
		if( this.trusting() || !slug )
			return;
		const rowLoad = this.#rowLoads.current;
		this.trusting.set( true );
		try{
			await this.gateway.postQL( ClientDetail.trustMutation, {opc: slug, thumbprint} );
		}
		catch( e ){
			if( this.#rowLoads.is(rowLoad) ){//else onRow reset it, and a trust on the row shown now may be in flight
				this.snackbar.exception( isRefusal(e) ? "Trust was refused." : "Could not trust the server.", e );
				this.trusting.set( false );
			}
			return;
		}
		if( !this.#rowLoads.is(rowLoad) )
			return;
		this.#announcer.announce( "Server trusted.  Reconnecting." );
		await this.#refreshConnection( slug, rowLoad );
		if( this.#rowLoads.is(rowLoad) )
			this.trusting.set( false );//not before:  the panel shows the certificate just trusted until the refresh reloads it, and a second press would post it again
	}
	//after a trust:  re-describe the connection - the reconnect is the gateway's, so re-read its result - and reload the certificates.
	async #refreshConnection( slug:string, rowLoad:number ){
		this.#opcStore.forget( this.gateway.slug, slug );
		let server:Server|undefined, error:string|undefined;
		try{ server = await this.#opcStore.getConnection( this.gateway, slug, {fresh: true} ); }
		catch( e ){ error = errorMessage( e ); }
		if( !this.#rowLoads.is(rowLoad) )
			return;
		this.server.set( server );
		this.serverError.set( error );
		this.identity.set( undefined );
		this.#resultFor.set( connectSettings(this.row) );
		await Promise.all( [this.#loadCertificates(), this.#loadOp()] );
	}
	//the rejected server certificate and the gateway's own, read from the row (no connect), so they show whether or not the
	//gateway can reach the server.  A new connection has neither.
	async #loadCertificates(){
		const slug = this.row?.slug;//optional:  a late load can land after the router has moved the page off its row
		if( !slug || this.isNew ){
			this.certificates.set( undefined );
			return;
		}
		const rowLoad = this.#rowLoads.current;
		try{
			const data = await this.gateway.query<{serverConnection:ConnectionCertificates|null}>( connectionCertificatesQuery, {opc: slug} );
			if( this.#rowLoads.is(rowLoad) )
				this.certificates.set( data.serverConnection ?? undefined );
		}
		catch{
			if( this.#rowLoads.is(rowLoad) )
				this.certificates.set( undefined );//the panel hides;  the row still loads
		}
	}
	//A saved Open Protocol row's `_Op` (docs/open-protocol/spec.md *Address space*), the eleven variables of its driver server read by id
	//through this session's client of the row, as the Server tab reads a server's diagnostics:  the Connection tab shows them in place of
	//the driver server's description, which says nothing of the controller.  A failure is said in their place;  the row still loads.
	//`read` false clears them, for a test of another kind, whose answer they are not.  Of two reads out at once, the later is shown.
	async #loadOp( read = true ){
		const row = this.row;
		const opLoad = this.#opLoads.next();
		this.op.set( undefined );
		this.opError.set( undefined );
		if( !read || !row?.slug || this.isNew || !row.isOpenProtocol ){
			this.opReading.set( false );
			return;
		}
		const rowLoad = this.#rowLoads.current;
		const current = ()=>this.#rowLoads.is( rowLoad ) && this.#opLoads.is( opLoad );
		this.opReading.set( true );
		try{
			const data = await this.gateway.query<{nodes:OpRow[]}>( opQuery, {opc: row.slug, id: opIds} );
			if( current() )
				this.op.set( readOp(data.nodes) );
		}
		catch( e ){
			if( current() )
				this.opError.set( errorMessage(e) );
		}
		if( current() )
			this.opReading.set( false );
	}
	//a mutation, POSTed:  it connects the gateway to the form's url and issues the slug's certificate, which a GET must not do.  The
	//settings go as the save would send them, null where the form leaves one unset, so the test tests the connection Save would save.
	//One selection for both kinds:  the gateway answers the fields of the kind it tested, a UA server's description or MID 0002's identity.
	static readonly testMutation = `mutation testConnection( opc: $opc, url: $url, kind: $kind, driver: $driver, certificateUri: $certificateUri, ${connectionSettingNames.map( n=>`${n}: $${n}` ).join( ", " )} ){ ${serverDescColumns} securityPolicyUri securityMode namespaces{ ${namespaceColumns} } ${identityColumns} }`;
	static readonly trustMutation = trustServerMutation;//a POST:  it changes the gateway's trust list, which a GET must not do
	//the rejected server certificate and the gateway's own for this connection (#415), read from the row;  the panel shows them
	trusting = signal( false );
	certificates = signal<ConnectionCertificates|undefined>( undefined );
	//what the Connection tab shows:  the resolver's describe or its failure (onRow), then the last test's.  A new connection has
	//neither until it is tested, and the tab says what the button does rather than why a session that was never tried is missing.
	server = signal<Server|undefined>( undefined );
	serverError = signal<string|undefined>( undefined );
	identity = signal<OpenProtocolIdentity|undefined>( undefined );//the controller a Test Connection on an Open Protocol form reached
	//a saved Open Protocol row's `_Op`, or why it could not be read;  both unset while it is read, and for any other row
	op = signal<OpDiagnostics|undefined>( undefined );
	opError = signal<string|undefined>( undefined );
	opReading = signal( false );
	get untested():boolean{ return this.isNew && !this.server() && !this.serverError() && !this.identity(); }//a getter, not a computed:  `row` is no signal, and the router reuses this page across connections
	testing = signal<string|undefined>( undefined );//the url under test while a Test Connection is in flight
	//the settings the shown result was for, where the form no longer holds them;  unset while the form matches.  An Open Protocol row's
	//driver settings are named by what changed in them, the zone or the ceilings, not by the json text.
	readonly staleFor = computed( ()=>{
		const was = this.#resultFor(), p = this.properties();
		if( !was || !p )
			return undefined;
		const now = connectSettings( p );
		const wasDriver = parseDriver( was.driver ), nowDriver = parseDriver( now.driver );
		const wasCeilings = ceilingsText( wasDriver.revisions ), nowCeilings = ceilingsText( nowDriver.revisions );
		const changed = [
			was.kind==now.kind ? "" : `Kind ${kindLabels[was.kind] ?? was.kind}`,
			was.url==now.url ? "" : `URL ${was.url}`,
			was.certificateUri==now.certificateUri ? "" : was.certificateUri ? `Certificate URI ${was.certificateUri}` : "an empty Certificate URI",
			wasDriver.timeZone==nowDriver.timeZone ? "" : wasDriver.timeZone ? `Time Zone ${wasDriver.timeZone}` : "an empty Time Zone",
			wasCeilings==nowCeilings ? "" : wasCeilings ? `Revision Ceilings ${wasCeilings}` : "no Revision Ceilings",
			was.slug==now.slug ? "" : `slug ${was.slug}`,
			...connectionSettingNames.map( n=>was[n]===now[n] ? "" : was[n]===null ? `an unset ${settingLabels[n]}` : `${settingLabels[n]} ${n=="securityMode" ? modeName(String(was[n])) : n=="securityPolicy" ? policyName(String(was[n])) : was[n]}` )
		].filter( (x)=>x );
		return changed.length ? changed.join( ", " ) : undefined;
	} );
	#resultFor = signal<ConnectSettings|undefined>( undefined );
	#rowLoads = new Latest();//onRow's:  a test answers only for the row it started on.
	#opLoads = new Latest();//#loadOp's:  onRow's read and a test's can be out at once
	#opcStore = inject( OpcStore );
	//the test's start and end, for a screen reader:  a region inserted with its text may go unannounced, and the tab's content -
	//a live region there included - is created with its text when the press switches to it.
	#announcer = inject( LiveAnnouncer );
	//the row's, set in onRow:  the router reuses this page across gateways
	gateway!:Gateway;
}